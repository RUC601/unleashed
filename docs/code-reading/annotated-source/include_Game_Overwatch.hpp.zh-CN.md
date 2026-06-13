# Annotated Source: include/Game/Overwatch.hpp

Source: D:/Desktop/SenseZen/ClaudeCodexCoding/Unleashed/include/Game/Overwatch.hpp
Snapshot: Git HEAD 10e87de + dirty working tree on 2026-06-10.
Purpose: reading notes only, not runtime source of truth.

EN: This note focuses on the entity pipeline inside `Overwatch.hpp`, not the whole file.
中文：这篇笔记只聚焦 `Overwatch.hpp` 里的实体管线，不覆盖整个文件。

## Mental Model

EN: `entity_scan_thread()` discovers raw entity pairs. `entity_thread()` decrypts and enriches them into `c_entity` snapshots.
中文：`entity_scan_thread()` 发现 raw entity pair；`entity_thread()` 把它们解密并补充成 `c_entity` 快照。

```text
EN: raw pair list -> component bases -> semantic fields -> roster -> published snapshot
中文：raw pair 列表 -> 组件基址 -> 语义字段 -> roster -> 发布快照
```

## Scan Thread

```cpp
inline void entity_scan_thread() {
    Diagnostics::ScopedDmaCallsite tag(Diagnostics::DmaCallsite::EntityScan);
    while (OW::Config::doingentity == 1) {
        if (!OW::ProcessConnection::IsConnected()) {
            Diagnostics::RecordEntityScanCycle(0, 0.0);
            Sleep(100);
            continue;
        }

        bool pending_scan = false;
        bool known_entities_empty = true;
        bool fast_rescan = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            pending_scan = OW::abletotread != 0;
            known_entities_empty = OW::ow_entities.empty() && OW::ow_entities_scan.empty();
        }
```

EN: The connection guard prevents stale process state from being used when the target process is disconnected.
中文：连接状态检查可以避免目标进程断开后继续使用旧进程状态。

EN: `pending_scan` means the consumer has not yet accepted the last produced scan result.
中文：`pending_scan` 表示消费者还没有接收上一批扫描结果。

```cpp
if (!pending_scan && scanDue) {
    std::vector<std::pair<uint64_t, uint64_t>> scanned = OW::get_ow_entities();
    Diagnostics::RecordEntityScanCycle(scanned.size(), entityScanHz);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (OW::abletotread == 0) {
            OW::ow_entities_scan = std::move(scanned);
            OW::abletotread = 1;
        }
    }
}
```

EN: The producer only publishes a new raw list when `abletotread == 0`.
中文：生产者只在 `abletotread == 0` 时发布新的 raw 列表。

EN: This small flag is the bridge between a lightweight scanner and the heavier decrypt/processing loop.
中文：这个小标记就是轻量扫描线程和较重解密处理线程之间的桥。

## Entity Thread Setup

```cpp
struct ComponentBaseCache {
    uint64_t linkParent = 0;
    uint64_t health = 0;
    uint64_t link = 0;
    uint64_t team = 0;
    uint64_t velocity = 0;
    uint64_t hero = 0;
    uint64_t bone = 0;
    uint64_t visibility = 0;
    bool healthValid = false;
    bool heroValid = false;
    bool isEnemy = false;
    bool vis = false;
    OW::c_entity::SkeletonBoneCache skeletonCache{};
};
```

EN: This cache stores component bases and slow-changing fields so the loop does not rediscover everything every tick.
中文：这个缓存保存组件基址和变化较慢的字段，避免每个 tick 都重新发现所有东西。

EN: Caches improve performance, but they also require validation and size limits. The current code clears them when they grow too large.
中文：缓存能提升性能，但也需要校验和规模限制。当前代码在缓存过大时会清空。

## Handoff From Scan To Processing

```cpp
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (OW::abletotread) {
        OW::ow_entities = OW::ow_entities_scan;
        OW::abletotread = 0;
    }
}

std::vector<std::pair<uint64_t, uint64_t>> raw_entities;
std::vector<OW::c_entity> previous_entities;
{
    std::lock_guard<std::mutex> lock(g_mutex);
    raw_entities = OW::ow_entities;
    previous_entities = OW::entities;
}
```

EN: Shared vectors are copied under the mutex and then processed outside the mutex.
中文：共享 vector 在 mutex 内拷贝，然后在 mutex 外处理。

EN: This pattern is important for avoiding long lock holds while DMA reads and component resolution happen.
中文：这个模式很重要，可以避免 DMA 读取和组件解析期间长时间持锁。

## Roster Snapshot

```cpp
auto publishRosterSnapshot = [&](DWORD now, size_t heroChanged, std::vector<OW::c_entity>& published) {
    published.clear();
    published.reserve(entityRoster.size());

    for (auto it = entityRoster.begin(); it != entityRoster.end();) {
        OW::c_entity& rosterEntity = it->second.entity;
        const bool seen = it->second.seenThisCycle;

        if (!seen && rosterEntity.roster_state != OW::EntityRosterState::Dead) {
            if (rosterEntity.missing_since_tick_ms == 0)
                rosterEntity.missing_since_tick_ms = now;
            rosterEntity.roster_state = OW::EntityRosterState::Missing;
        }

        published.push_back(rosterEntity);
        ++it;
    }
};
```

EN: The roster keeps continuity between raw scans. An entity can be fresh, missing, dead, or expired.
中文：roster 负责在多次 raw scan 之间保持连续性。实体可以是 fresh、missing、dead 或 expired。

EN: This is why downstream code can reason about disappearing targets more gracefully than if the vector simply dropped them immediately.
中文：因此下游代码处理突然消失的目标时会更平滑，而不是 vector 立刻删掉目标。

## Empty Raw List Behavior

```cpp
if (raw_entities.empty()) {
    std::vector<OW::c_entity> published_entities{};
    Diagnostics::RosterStats rosterStats = publishRosterSnapshot(processLoopTick, 0, published_entities);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        OW::entities = std::move(published_entities);
        OW::hp_dy_entities = {};
        if (rosterStats.fresh > 0 || rosterStats.missing > 0 || rosterStats.dead > 0)
            OW::entity_fast_scan_until_tick = GetTickCount() + OW::kEntityFastRescanWindowMs;
    }
    Sleep(16);
    continue;
}
```

EN: Empty raw scan does not simply mean "clear everything forever". The roster snapshot can still publish transitional state.
中文：raw scan 为空不等于“永远清空所有东西”。roster 快照仍可能发布过渡状态。

EN: Fast rescan is scheduled when the roster still has meaningful state, which helps the pipeline recover quickly.
中文：当 roster 仍有有意义状态时会安排 fast rescan，帮助管线快速恢复。

## Render Sample Carry-Forward

```cpp
auto attachPreviousRenderSample = [&](OW::c_entity& entity) {
    entity.render_sample_tick_ms = processLoopTick;

    const OW::c_entity* previousEntity = nullptr;
    const auto previousIt = previousEntityByAddress.find(entity.address);
    if (previousIt != previousEntityByAddress.end())
        previousEntity = previousIt->second;

    if (!previousEntity) {
        entity.previous_render_sample_tick_ms = processLoopTick;
        entity.has_previous_render_sample = false;
        entity.previous_head_pos = entity.head_pos;
        entity.previous_velocity = entity.velocity;
        return;
    }
};
```

EN: This preserves previous-frame context for rendering and movement smoothing.
中文：这里保存上一帧上下文，用于渲染和移动平滑。

EN: If overlay movement looks jittery, this is one of the areas worth reading with `Structs.hpp` render-sample fields open beside it.
中文：如果 overlay 移动看起来抖，这里值得和 `Structs.hpp` 里的 render-sample 字段一起读。

## Common Misread

EN: `ow_entities`, `ow_entities_scan`, and `entities` are not the same layer.
中文：`ow_entities`、`ow_entities_scan`、`entities` 不是同一层。

```text
ow_entities_scan:
  latest raw scan batch waiting for the consumer

ow_entities:
  raw batch accepted by entity_thread

entities:
  published semantic c_entity snapshot for downstream runtime
```

EN: When debugging target selection, usually start from `entities`, not from `ow_entities_scan`.
中文：排查目标选择时，通常应该从 `entities` 开始，而不是从 `ow_entities_scan` 开始。
