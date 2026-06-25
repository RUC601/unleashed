# Render / Entity / DMA 回归排查记录

时间：2026-06-24

代码快照：`873e27d`

日志样本：

- `build/Release/unleashed_diag.log`，最后写入 `2026-06-19 12:44:25`
- `build/Release/unleashed_aim_diag.log`，最后写入 `2026-06-19 12:44:25`

## 结论

当前最可疑的主因不是单一优化点失效，而是三类问题叠加：

1. render thread 仍在执行同步 DMA / RPM / read_range、技能运行时逻辑、输入轮询和同步日志写入，不是纯绘制线程。
2. `entity_thread` 的 `16ms` 目标周期只是最小启动间隔；loop body 中 per-entity 同步读取、解密、骨骼、team/skill refresh 等工作已经把实际处理频率压到 `2-4Hz`。
3. slow-frame 诊断本身会在持续慢帧时同步统计 DMA window、写文件并 flush，形成额外反馈负载。

`da086ba/873e27d` 的 scatter/prefetch 结论要分开看：

- hot-field scatter 在当前日志里是命中的，`velocity/health/hero/visibility` 均没有 fallback 放大。
- scatter 仍是“每实体 execute”，不是跨实体批量，因此只能减少单实体内部字段读取次数，不能解决整体 per-entity 同步工作量。
- prefetch 受环境变量控制，当前日志没有 `DMA-PREFETCH` 记录，不能把本轮慢帧归因到 prefetch。

## P1 Findings

### P1-1 Render thread 不是纯绘制，仍执行同步 DMA/read_range/RPM、skill processing、input polling 和日志 IO

性质：旧问题，但被近期 render workload 诊断、snapshot 消费增加和 `boxPerfMode` 对照放大。

证据：

- `src/Renderer/Overlay.cpp:940-944`
  - `Overlay::RenderCanvas()` 用 `Diagnostics::ScopedDmaCallsite(RenderCanvas)` 包住整个 `renderCallback()`。
  - 因此 render callback 内发生的 DMA 会被记为 render-side DMA。
- `src/main.cpp:996-1006`
  - `RenderCallback()` 每帧调用 `Diagnostics::RecordFrame()`、`BeginRenderWorkloadFrame()`。
  - 在 `!boxPerfMode` 且 connected 时调用 `OW::ProcessHeroSkills()`。
- `src/main.cpp:1014`
  - render callback 中调用 `OW::TargetingDetail::SnapshotEntities().empty()`，只是判空也会完整拷贝一次实体 vector。
- `src/main.cpp:1033-1052`
  - normal render path 继续执行 radar、`PlayerInfo(false)`、`skillinfo()`、FOV、deadzone、crosshair、diagnostic overlay 等。
- `include/Game/Overwatch.hpp:3919-3922`
  - `PlayerInfo()` 进入后又调用 `SnapshotEntities()` 和 `SnapshotLocalEntity()`。
- `include/Game/Overwatch.hpp:4091-4095`
  - `PlayerInfo()` 在若干视觉路径下调用 `GetHeroEngNames(entity.HeroID, entity.LinkBase)`。
- `include/Game/Decrypt.hpp:1498-1503`
  - D.Va 路径会对 `LinkBase + 0xD4/0xD8` 执行 `SDK->RPM<uint16_t>()`。
- `src/Game/HeroSkills.cpp:3911-4020`
  - `ProcessHeroSkills()` 包含 runtime 技能处理。
- `src/Game/HeroSkills.cpp:3961-3964`
  - ammo / reload guard 路径会读取 `AimbotDetail::IsInputVkDown('R')`，即 render callback 间接进入输入状态查询。
- `build/Release/unleashed_diag.log:379`
  - 早期 slow frame 已显示 `rtDma[reads=76] RenderCanvas[rd=76]`。
- `build/Release/unleashed_diag.log:2147`
  - 典型样本：`SLOW_FRAME total=236.5ms render=230.3ms ... rtDma[reads=76 ... max=160770us] ... RenderCanvas[rd=60 mx=160770us]`。

影响：

- render thread 会被 DMA 延迟尖峰直接卡住，导致 present 前的 frame callback 拉长。
- `ProcessHeroSkills()`、`PlayerInfo()`、`skillinfo()` 和若干 diagnostics 都在同一帧回调里叠加。
- 即使部分 UI 绘制被优化，render thread 仍可能因为非绘制逻辑慢下来。

最小验证：

1. 临时让 `RenderCallback()` 只消费一份预生成 snapshot，不调用 `ProcessHeroSkills()`、`PlayerInfo()`、`skillinfo()` 中任何可能 DMA 的路径。
2. 运行 30 秒，确认 slow-frame 日志中的 `rtDma[reads]` 和 `RenderCanvas[rd]` 是否降到 `0`。
3. 分别恢复 `PlayerInfo()`、`skillinfo()`、`ProcessHeroSkills()`，观察是哪一路重新引入 render-side DMA。

最小修复建议：

- render callback 只读 frame snapshot，不做 DMA、RPM、read_range。
- 将 hero name、skill info、health pack、target display 等数据预计算到 entity/runtime 线程。
- 将 `ProcessHeroSkills()` 从 render callback 移到独立 fixed-rate runtime 线程，或至少移到非 render path。
- 同一帧内复用一份 snapshot，避免多个 UI panel 各自 `SnapshotEntities()`。

### P1-2 `entity_thread` 的目标周期是 16ms，但实际处理频率被 loop body 压到 2-4Hz

性质：旧的 per-entity 同步读取/解密/骨骼/技能刷新问题，被近期字段和 snapshot 消费增加放大；不是当前日志里的 scatter fallback 问题。

证据：

- `include/Game/Overwatch.hpp:1053-1057`
  - `kEntityProcessIntervalMs` 只在未满 16ms 时 sleep。
  - 如果 loop body 本身耗时远超 16ms，代码不会抢占或跳过本轮工作。
- `include/Game/Overwatch.hpp:914-928`
  - `recordEntityCycle()` 每 60 cycles 才更新一次 `entityCycleHz`，显示值会滞后并阶梯跳变。
- `build/Release/unleashed_diag.log:920`
  - `process_hz=4.5`。
- `build/Release/unleashed_diag.log:1695`
  - `process_hz=2.4`。
- `build/Release/unleashed_diag.log:2135`
  - `process_hz=2.4`，同时 DMA 总量和失败数继续增长。
- `build/Release/unleashed_diag.log:2139`
  - `[DMA-FIELD] window=5140ms cycles=14`，约 `2.7Hz`。
- `include/Game/Overwatch.hpp:1189-1249`
  - `readHotFields()` 每实体组织 hot-field scatter，并在 `1247` 执行一次 scatter。
- `include/Game/Overwatch.hpp:1492-1519`
  - component header、link header、component pointer、component id 等仍有同步读取。
- `include/Game/Overwatch.hpp:1554-1565`
  - component decrypt 路径仍在 per-entity loop 内执行。
- `include/Game/Overwatch.hpp:1590`
  - `Entity_MatchId` 仍有同步 RPM。
- `include/Game/Overwatch.hpp:1971`
  - skeleton cache 路径仍会进入骨骼读取。
- `include/Game/Overwatch.hpp:2228-2230`
  - team/skill refresh 仍在 entity loop 内触发。
- `include/Game/Overwatch.hpp:2321`
  - local cooldown / skill 状态读取仍在同一线程内。

影响：

- 16ms 目标只限制“太快时 sleep”，不能保证“慢时按 60Hz 处理”。
- `process_hz` 降到 2-3Hz 时，snapshot 新鲜度会明显下降。
- render thread 若同时竞争 DMA，会进一步拉长 entity loop 中同步读的尾延迟。

最小验证：

1. 在 `entity_thread` 单 cycle 首尾记录 `cycle_ms`、`raw_entities`、`validated`、`published`、`dma_reads_delta`。
2. 同时记录 `hot_scatter_execute`、component/header/decrypt/bone/skill/local 各阶段耗时。
3. 先关闭 render-thread DMA 路径再采样，对比 `cycles/window_ms` 是否恢复。

最小修复建议：

- 将 entity loop 拆成 fast dynamic fields 与 slow refresh 两层。
- velocity/health/visibility/position 走高频快速发布。
- team、skill、hero name、bone、component refresh 降频或增量刷新。
- scatter 从“每实体 execute”改为“跨实体批量 execute”，失败项再 fallback。
- `process_hz` 改为短窗口/EMA，避免 60-cycle 平均造成误判。

## P2 Findings

### P2-1 scatter/prefetch 当前没有证据显示 fallback 放大，但 scatter 粒度仍偏细

性质：近期优化部分有效；不是当前日志里的主要回归源。

证据：

- `include/Game/Overwatch.hpp:569-579`
  - hot page prefetch 受 `UNLEASHED_DMA_PREFETCH_HOT_PAGES` 控制，默认未启用。
- `include/Game/Overwatch.hpp:648-657`
  - 只有启用时才会输出 `[DMA-PREFETCH]`。
- 当前 `build/Release/unleashed_diag.log` 中没有 `DMA-PREFETCH` 记录。
- `include/Game/Overwatch.hpp:1189-1249`
  - `readHotFields()` 会将 velocity/health/hero/visibility 放入 scatter，但每个实体执行一次 `ExecuteReadScatter()`。
- `src/Memory/Memory.cpp:1604-1632`
  - scatter wrapper 每次 execute 记录一次 DMA read。
- `build/Release/unleashed_diag.log:2141`
  - velocity：`req=293 scatter_hit=293 partial=0 fallback=0 fail=0`
  - health：`req=293 scatter_hit=293 partial=0 fallback=0`
- `build/Release/unleashed_diag.log:2142`
  - hero：`req=228 scatter_hit=228 partial=0 fallback=0`
  - visibility：`req=228 scatter_hit=228 partial=0 fallback=0`

影响：

- 当前 hot-field scatter 没有退化成大量 fallback 同步读。
- 但每实体 execute 仍会产生大量 DMA 调用，无法解决整体 entity loop 过慢。

最小验证：

- 增加一项 debug counter：每个 process cycle 的 scatter execute 次数、每次 execute 的 request 数、fallback 数。
- 对比“每实体 execute”和“跨实体批量 execute”的 DMA reads/s、cycle_ms、process_hz。

最小修复建议：

- 保留当前 fallback 安全性。
- 将 hot fields 跨实体合并成少数 scatter handles。
- 只对 partial/fail 的 item 做单读 fallback。

### P2-2 tracking/aim 热路径存在同步 Diagnostics::Aim、mutex、flush、SnapshotEntities 大拷贝

性质：旧问题，被近期诊断量和慢帧频率放大；`873e27d` 已把 aim flush 从每行降低到节流，但仍是同步写。

证据：

- `src/Utils/Diagnostics.cpp:395-420`
  - `AimLogV()` 同步格式化、写 ring、锁 `g_aimLogMutex`、写文件，并按 250ms flush。
- `include/Game/Overwatch.hpp:5149-5184`
  - `LogAimKeyState()` 会向 aim log 写 hotkey 状态。
- `include/Game/Overwatch.hpp:5200-5208`
  - KMBox monitor 状态变化也写 aim log。
- `include/Game/Overwatch.hpp:5217-5242`
  - DMA key state resolver 不可用、状态变化等也写 aim log。
- `include/Game/Overwatch.hpp:5277-5315`
  - `IsInputVkDown()` 会串起 KMBox、DMA、local input fallback 和日志。
- `include/Game/Overwatch.hpp:6483-6663`
  - tracking loop 中 `GetVector3()`、no-target 日志、actionable 检查会进入 target/snapshot 路径。
- `include/Game/Target.hpp:1065-1068`
  - `SnapshotEntities()` 锁 `g_mutex` 后按值返回完整 `std::vector<c_entity>`。
- `include/Game/Entity.hpp:84-108`
  - `c_entity` 包含 skeleton、previous skeleton、位置、状态等大量字段，拷贝成本高。
- `build/Release/unleashed_aim_diag.log`
  - 当前样本中 `hotkey state` 日志约 6053 条，`sequence.ammo_guard` 约 113 条。

影响：

- aim/tracking 活跃时，热路径可能同时承受 snapshot 大拷贝、target selection、同步日志锁和文件写。
- 当前样本没有 `target.primary start/result`，说明该日志窗口内主瞄准路径未充分激活；代码风险仍存在。

最小验证：

1. 临时禁用 `AimLogV()` 文件写和 flush，只保留内存计数。
2. aim key held 状态采样 30 秒，对比 `render_ms`、`aimbot.summary`、`SLOW_FRAME`。
3. 增加 `SnapshotEntities()` 调用次数和估算 copy bytes/s。

最小修复建议：

- `Diagnostics::Aim` 改异步队列写盘。
- hotkey false 状态日志限频，状态未变化不写。
- target/tracking 单 tick 内复用一份 snapshot。
- `HasEntitySnapshot()` 改成 atomic published count，不要通过完整拷贝判空。

### P2-3 `873e27d` 将 autobone / closest bone 收窄到 head/neck/chest，可能导致目标向量短暂归零

性质：近期语义回归。

证据：

- `include/Game/Target.hpp:1138-1175`
  - `TrySelectClosestCoreAimPoint()` 只遍历 `BONE_HEAD`、`BONE_NECK`、`BONE_CHEST`。
- `include/Game/Target.hpp:1656-1665`
  - `SelectAutoBone()` 忽略 `maxSkeletonBones`，core miss 后直接返回 `Vector3{}`。
- `src/Game/HeroSkills.cpp:1502-1526`
  - skill aim point 的 closest-bone 也优先 core-only，不过部分技能路径仍有 configured/candidate fallback。
- `src/Game/HeroSkills.cpp:1554-1601`
  - auto melee closest path 同样限制在 core bones。

影响：

- 以前 full skeleton fallback 可用时，即使 head/neck/chest 暂时无效，也可能选到其他有效骨骼。
- 现在 core bones 缺失、为零、等于 entity position 或非 finite 时，`SelectAutoBone()` 可能直接给零向量。
- 这会表现为目标向量短暂归零、aim/tracking 短时 no target 或停顿。

最小验证：

- 增加计数：`autobone_core_miss`、`any_skeleton_valid`、`returned_zero`。
- 与 `873e27d^` 对比同一窗口内 zero aim vector 次数。

最小修复建议：

- core miss 后 fallback 到 last valid aim point。
- 再 fallback 到 configured bone / candidate position。
- 最后按 `maxSkeletonBones` 扫 full skeleton，避免有可用骨骼时返回零向量。

### P2-4 slow-frame 日志可能形成反馈循环

性质：旧诊断机制，被持续 slow frame 放大。

证据：

- `src/Utils/Diagnostics.cpp:1201-1263`
  - `RecordFrameTiming()` 在每个 slow frame 中读取并格式化 DMA window、callsite stats。
- `src/Utils/Diagnostics.cpp:1265-1288`
  - `GetDmaWindowStats()` 遍历 DMA ring samples。
- `src/Utils/Diagnostics.cpp:380-392`
  - `Warn()` / `Error()` 会同步写日志并 flush。
- 当前 `build/Release/unleashed_diag.log` 中 `SLOW_FRAME` 约 1313 条。

影响：

- 当每帧都慢时，诊断代码会每帧额外做统计、字符串格式化、文件写和 flush。
- 这不会是 DMA 根因，但会扩大 CPU/IO 抖动，让慢帧更难恢复。

最小验证：

- 将 slow-frame 日志限频到每秒一次，保留内存计数。
- 对比限频前后的 `render_ms`、`present_ms`、`SLOW_FRAME` 数和日志写入量。

最小修复建议：

- slow-frame 只记录 first N、状态变化或 1s 聚合摘要。
- warning 级别不要每帧 flush。
- DMA window 统计用后台聚合，render thread 只读聚合结果。

## P3 Findings

### P3-1 `boxPerfMode` 把“少画东西”和“停掉 ProcessHeroSkills 运行时逻辑”耦合在一起

性质：近期回归 / 诊断开关设计风险。

证据：

- `src/main.cpp:1001-1006`
  - `ProcessHeroSkills()` 只有在 `!boxPerfMode` 时执行。
- `src/main.cpp:1018-1030`
  - `boxPerfMode` 分支只跑 `PlayerInfo(true)` 后提前 return。
- `src/main.cpp:1506`
  - CLI 参数 `--box-perf-mode`。
- `src/main.cpp:1534-1537`
  - CLI 打开后同时设置 `boxPerfMode=true` 和 `boxPerfFastRect=true`。
- `src/main.cpp:1557-1564`
  - 日志只表达为 perf/fast rect 模式，未明确提示技能 runtime 被停用。

影响：

- 使用者以为只是少画东西，但实际会改变 runtime skill 行为。
- 这会污染性能对比：FPS 提升可能来自停掉技能逻辑，而不只是绘制减少。

最小验证：

- 开启 `boxPerfMode` 后检查 `sequence.ammo_guard`、skill action、runtime skill counter 是否停止。

最小修复建议：

- 拆分为 `boxVisualPerfMode` 和 `suspendHeroSkillRuntime`。
- visual perf mode 只影响绘制内容。
- skill runtime 的暂停必须是独立、显式的调试开关。

### P3-2 SnapshotEntities 大拷贝已经成为横跨 render、aim、test/debug 的共同放大器

性质：旧问题，被近期 snapshot 模型和更多消费端放大。

证据：

- `include/Game/Target.hpp:1065-1068`
  - `SnapshotEntities()` 锁后按值返回完整 `entities`。
- `include/Game/Target.hpp:1070-1072`
  - `SnapshotDynamicEntities()` 同样按值拷贝 dynamic vector。
- `include/Game/Target.hpp:1075-1077`
  - `SnapshotLocalEntity()` 也在同一锁下复制 local。
- `src/main.cpp:1014`
  - render callback 判空也通过 `SnapshotEntities()`。
- `include/Game/Overwatch.hpp:3921`
  - `PlayerInfo()` 每次 snapshot。
- `include/Game/Overwatch.hpp:4207`
  - `skillinfo()` 每次 snapshot。
- `include/Game/Overwatch.hpp:5403-5405`
  - `HasEntitySnapshot()` 只为判空也完整拷贝。
- `include/Game/Target.hpp:1739`
  - `AcquireTarget()` 每次 target acquisition snapshot。
- `include/Game/Target.hpp:2030`
  - `TryGetTargetEntity()` snapshot 后查找。

影响：

- 单次拷贝成本随 `c_entity` 字段扩张而增长。
- render/aim/test/debug 多消费者同时运行时，mutex 和拷贝会叠加。

最小验证：

- 在 snapshot 函数内统计调用次数、entities 数量、估算 copy bytes/s。
- 对比 TestServer off、TestServer on 但不访问接口、持续访问 `/api/entities` 三种场景。

最小修复建议：

- 发布不可变 frame snapshot，用 `shared_ptr<const Snapshot>` 或双缓冲结构传递。
- render frame 内只获取一次 snapshot 并传给各 panel。
- `HasEntitySnapshot()` 改为 atomic published count。
- TestServer JSON 序列化使用独立轻量 DTO，不直接频繁复制完整 `c_entity`。

## 最小验证顺序建议

1. 先做 render-thread DMA 清零实验。
   - 目标：确认 `rtDma[reads]` 和 `RenderCanvas[rd]` 是否能降到 `0`。
   - 这是分离 render 卡顿和 entity loop 卡顿的最快方法。
2. 给 entity loop 加阶段耗时和 read delta。
   - 目标：确认每轮到底慢在 component/decrypt/bone/skill/local 哪一段。
3. 对 snapshot 做计数和 copy bytes 估算。
   - 目标：确认 render/aim/TestServer 是否在高频复制完整 entity vector。
4. 对 slow-frame log 限频。
   - 目标：确认诊断 IO 是否在持续慢帧时明显放大总耗时。
5. 对 autobone 增加 core miss / zero vector counter。
   - 目标：确认 `873e27d` 的 core-only 收窄是否导致实际目标向量归零。

## 最小修复优先级

1. render callback 只消费 snapshot，不做 DMA、不跑 skill runtime。
2. `ProcessHeroSkills()` 移到独立 runtime tick，和 visual perf mode 解耦。
3. entity loop 拆 fast/slow refresh，并把 scatter 粒度从 per-entity 提升到跨实体 batch。
4. snapshot 发布改为不可变共享帧，减少全量 vector 拷贝。
5. slow-frame / aim / hotkey 日志改异步或限频。
6. autobone core miss 恢复 fallback，不再直接返回零向量。

## 本轮未做事项

- 未修改业务代码。
- 未运行 `.\build-release.ps1`。
- 未做 live A/B 采样；本记录基于当前源码和 `2026-06-19 12:44` 的历史日志样本。
