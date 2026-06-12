# 02 Entity Pipeline

Source: D:/Desktop/ClaudeCodexCoding/Unleashed/include/Game/Overwatch.hpp
Snapshot: Git HEAD 10e87de + dirty working tree on 2026-06-10.
Purpose: reading notes only, not runtime source of truth.

EN: This note explains how raw entity pairs become the `c_entity` snapshots consumed by targeting and rendering.
中文：这篇笔记解释 raw entity pair 如何变成瞄准和渲染消费的 `c_entity` 快照。

## Two-Stage Shape

```text
entity_scan_thread()
  -> calls get_ow_entities()
  -> publishes raw pairs into ow_entities_scan

entity_thread()
  -> consumes ow_entities_scan through ow_entities
  -> resolves component bases, health, hero, visibility, bones, skills
  -> publishes OW::entities and OW::local_entity snapshots
```

EN: The scan thread is intentionally light. It discovers candidates.
中文：扫描线程故意保持轻量，它负责发现候选实体。

EN: The entity thread is heavier. It turns candidates into usable semantic state.
中文：实体处理线程更重，它把候选项转换成可用的语义状态。

## Scan Producer

```cpp
while (OW::Config::doingentity == 1) {
    if (!OW::ProcessConnection::IsConnected()) {
        Diagnostics::RecordEntityScanCycle(0, 0.0);
        Sleep(100);
        continue;
    }

    const bool scanDue = lastScanTick == 0 || now - lastScanTick >= scanInterval;
    if (!pending_scan && scanDue) {
        std::vector<std::pair<uint64_t, uint64_t>> scanned = OW::get_ow_entities();
        Diagnostics::RecordEntityScanCycle(scanned.size(), entityScanHz);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (OW::abletotread == 0) {
            OW::ow_entities_scan = std::move(scanned);
            OW::abletotread = 1;
        }
    }
    Sleep(OW::kEntityScannerIdleSleepMs);
}
```

EN: `abletotread` is the handoff flag. The scan thread only replaces `ow_entities_scan` when the consumer has taken the previous batch.
中文：`abletotread` 是交接标记。扫描线程只在消费者已经拿走上一批数据后，才替换 `ow_entities_scan`。

EN: This prevents the producer from constantly overwriting a batch while the consumer is still using it.
中文：这样可以避免生产者在消费者还没处理完时不断覆盖同一批数据。

## Processing Consumer

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

EN: The consumer copies shared state into local variables, then does heavy work outside the lock.
中文：消费者先把共享状态拷贝到局部变量，然后在锁外做重活。

EN: This pattern matters because target selection and rendering also need consistent snapshots.
中文：这个模式很重要，因为目标选择和渲染也需要一致的快照。

## Component And Roster Caches

```cpp
struct ComponentBaseCache {
    uint64_t linkParent = 0;
    uint64_t health = 0;
    uint64_t hero = 0;
    uint64_t bone = 0;
    uint64_t visibility = 0;
    bool healthValid = false;
    bool heroValid = false;
    bool isEnemy = false;
    bool vis = false;
    OW::c_entity::SkeletonBoneCache skeletonCache{};
};

std::unordered_map<uint64_t, ComponentBaseCache> componentBaseCache{};
```

EN: Component lookups are expensive and can be partially stable across cycles. The cache keeps repeated reads from becoming worse than necessary.
中文：组件查找开销较大，而且部分组件在多个周期内相对稳定。缓存可以减少重复读取造成的额外负担。

EN: The roster layer is not the same as the raw scan list. It tracks continuity: fresh, missing, dead, and expired entities.
中文：roster 层不等于 raw scan 列表。它负责追踪连续性：fresh、missing、dead、expired。

## Snapshot Publishing

```cpp
auto publishRosterSnapshot = [&](DWORD now, size_t heroChanged, std::vector<OW::c_entity>& published) {
    published.clear();
    published.reserve(entityRoster.size());

    for (auto it = entityRoster.begin(); it != entityRoster.end();) {
        OW::c_entity& rosterEntity = it->second.entity;
        const bool seen = it->second.seenThisCycle;

        if (!seen && rosterEntity.roster_state != OW::EntityRosterState::Dead) {
            rosterEntity.roster_state = OW::EntityRosterState::Missing;
        }

        published.push_back(rosterEntity);
        ++it;
    }
};
```

EN: Published entities are the stable product of the pipeline. Downstream code should prefer snapshots instead of walking raw memory itself.
中文：发布出去的 entities 是管线的稳定产物。下游代码应该优先消费快照，而不是自己重新遍历 raw memory。

EN: Missing or dead entities may still be published briefly so rendering and targeting can transition smoothly instead of snapping.
中文：missing 或 dead 的实体可能会短暂保留在发布快照里，这样渲染和目标选择可以更平滑地过渡，而不是突然跳变。

## Why Render Samples Exist

```cpp
auto attachPreviousRenderSample = [&](OW::c_entity& entity) {
    entity.render_sample_tick_ms = processLoopTick;
    entity.previous_head_pos = entity.head_pos;
    entity.previous_velocity = entity.velocity;
    entity.previous_pos = entity.pos;
};
```

EN: Render samples preserve previous positions so overlay drawing can interpolate or stabilize movement.
中文：render sample 保存上一帧位置，让 overlay 绘制时可以插值或稳定移动。

EN: This is a good example of why the entity pipeline is not just "read memory and push vector".
中文：这也说明实体管线不只是“读内存然后塞进 vector”这么简单。
