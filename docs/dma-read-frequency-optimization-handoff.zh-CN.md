# DMA 读取频率优化交接文档

更新时间：2026-06-19
工作目录：`D:\Desktop\SenseZen\ClaudeCodexCoding`
目标项目：`D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed`

## 给新线程的目标模式建议

建议把目标设成：

> 在 `D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed` 中，基于当前 live DMA 环境，先建立实体读取频率、DMA 请求、失败率和慢帧基线，再做小范围代码优化，减少 `entity_thread` 高频路径的小随机 DMA 读，提高已知实体动态字段的有效刷新率；保持 scan/process/snapshot 架构不变，完成 `.\build-release.ps1` 和 live 日志/TestServer 对比验证，输出优化前后的 `process_hz`、`scan_hz`、`dma_reads/s`、失败率、延迟、`SLOW_FRAME`、实体数量和本地实体稳定性评估。

这个目标的重点不是“把某个 Sleep 改小”，而是“用证据证明有效刷新变快或证明当前瓶颈在哪里”。如果 live 环境波动导致不能稳定复现，也要留下基线、实验记录和下一步最小改动建议。

## 背景和来龙去脉

用户正在比较 Rigel 的线程模型和本项目的 UN-DMA 读取模型。

Rigel 是单机 external，所有线程在一个 Windows 进程里，读内存和共享 `entities` 的成本较低。它的基本形态是：

- `entity_scan_thread` 扫实体入口。
- `entity_thread` 读取/解密/汇总实体字段，发布全局 `entities` 和 `local_entity`。
- `viewmatrix_thread` 高频读取矩阵。
- `aimbot_thread` 和 overlay 多数消费同一份全局实体快照，只额外读少量本地角度、灵敏度等字段。

本项目当前形态类似 Rigel 的“线程按职责分工 + 共享快照”，但 UN-DMA 对大量小随机读更敏感。这里的优化方向不应是让多个线程分别读实体字段，而应是集中 DMA 读取、批量化、分层刷新，再让 aim/render/test server 消费快照。

## 当前代码形态

关键文件：

- `Unleashed/include/Game/Overwatch.hpp`
- `Unleashed/include/Game/Target.hpp`
- `Unleashed/src/Memory/Memory.cpp`
- `Unleashed/include/Utils/Diagnostics.hpp`
- `Unleashed/src/Utils/Diagnostics.cpp`
- `Unleashed/src/Utils/TestServer.cpp`
- `Unleashed/src/main.cpp`

当前核心全局状态在 `Overwatch.hpp`：

- `OW::viewMatrix` / `OW::viewMatrix_xor`
- `OW::entities`
- `OW::hp_dy_entities`
- `OW::local_entity`
- `OW::ow_entities`
- `OW::ow_entities_scan`

当前线程分工：

- `entity_scan_thread()`：扫描 raw entity pair，写 `ow_entities_scan`。
- `entity_thread()`：从 raw pair 构造/刷新实体，写 `OW::entities`、`OW::hp_dy_entities`、`OW::local_entity`。
- `viewmatrix_thread()`：高频读取 view/projection/camera matrices。
- `aimbot_thread()`：主要消费 `TargetingDetail::SnapshotEntities()` / `SnapshotLocalEntity()`，但仍有少量本地角度、灵敏度等即时读。
- render callback / overlay：应消费快照，不应直接读 DMA。

当前 snapshot helper：

- `TargetingDetail::SnapshotEntities()`
- `TargetingDetail::SnapshotDynamicEntities()`
- `TargetingDetail::SnapshotLocalEntity()`
- `OW::SnapshotViewMatrix()`
- `OW::GetViewMatricesSnapshot(...)`

不要绕开这些 helper 去直接读共享 vector。

## 当前频率和基线

当前 constexpr 节奏在 `Overwatch.hpp`：

```cpp
inline constexpr DWORD kEntityScanIntervalMs = 250;
inline constexpr DWORD kEntityEmptyScanIntervalMs = 50;
inline constexpr DWORD kEntityProcessIntervalMs = 16;
inline constexpr DWORD kEntitySlowFieldIntervalMs = 500;
inline constexpr DWORD kEntityHealthIntervalMs = kEntityProcessIntervalMs;
inline constexpr DWORD kEntityHeroIntervalMs = 50;
inline constexpr DWORD kEntityDeadComponentRefreshMs = 250;
inline constexpr DWORD kEntityLiveComponentRefreshMs = 2000;
inline constexpr DWORD kEntityFastRescanWindowMs = 2000;
inline constexpr DWORD kEntityScannerIdleSleepMs = 5;
```

注意：`kEntityProcessIntervalMs = 16` 只是目标节奏，实际日志显示没有达到 62.5Hz。

最近一次本地 live 日志基线来自：

- `Unleashed\build\Release\unleashed_diag.log`
- 时间段：2026-06-19 10:45:40 到 10:47:10 左右

观察到的典型状态：

- `entities=7`
- `last_scan=22`
- `scan_hz=0.1-0.2`
- `process_hz=11.7-16.8`
- `fps` 曾有正常 60 左右，也出现过 0 或异常值，需结合窗口状态判断。
- `dma_reads` 在 5 秒窗口内大约增加 29k-31k，粗略约 5.8k-6.2k reads/s。
- `latency_us[min/avg/max]` 约 `0/463-467/190734`。
- `fail` 持续增长，约 5% 左右量级，具体请重新取当前 live 基线。
- 出现过 `SLOW_FRAME total=52.7ms`，其中 100ms DMA 窗口有 `600` reads：
  - `EntityScan[rd=262]`
  - `EntityDecrypt[rd=240]`
  - `ViewMatrix[rd=87]`
  - `Aimbot[rd=11]`

结论：现在不是单纯 Sleep 太长，而是每轮 DMA 读/解包工作量已经把 `entity_thread` 拉低到十几 Hz。优化要从减少请求数量、减少失败读、批量化和分层刷新入手。

## 最重要的架构约束

必须保持：

- render/UI/test server 消费已发布 snapshot，不直接做新的实体 DMA 读取。
- `entity_scan_thread` 负责发现，不追求高频扫全表。
- `entity_thread` 是实体字段读取和发布的主路径。
- `viewmatrix_thread` 可以继续作为少量高频读路径。
- aim 允许少量本地即时读，但不要让 aim 读完整实体数据包。
- 不改 offset/profile 语义，除非优化过程中证明某个读路径本身是错误的。
- 不建立新的 build 目录；规范 build 命令是：

```powershell
cd D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed
.\build-release.ps1
```

不要做：

- 不要简单把 `kEntityProcessIntervalMs` 从 16 改成 8 就宣称优化。
- 不要新增多个 worker 分别读 health/team/bone/visibility，制造更多随机 DMA 请求。
- 不要让 overlay/render 线程直接 RPM/read_range。
- 不要为优化重写 pipeline 或引入大型依赖。
- 不要用旧日志当最终结论，必须重新取 live 基线。

## 推荐工作路线

### 1. 先建立当前 live 基线

在用户尽量保持 live 验证环境开启的前提下，先确认进程和日志：

```powershell
cd D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed
Get-Process | Where-Object { $_.ProcessName -match 'Unleashed|overlay|imgui|leech|dma|kmbox|sense|scenario' } |
  Select-Object Id,ProcessName,Path
```

读取最近 STATUS：

```powershell
Select-String -Path .\build\Release\unleashed_diag.log -Pattern "STATUS entities=|SLOW_FRAME" |
  Select-Object -Last 40
```

如果 test server 开着，优先用实际端口访问：

```powershell
Invoke-RestMethod http://127.0.0.1:<port>/api/health
Invoke-RestMethod http://127.0.0.1:<port>/api/entities
Invoke-RestMethod http://127.0.0.1:<port>/api/local
```

如果不知道端口，先从运行参数、日志或用户上下文确认；不要猜。

基线至少记录 30-60 秒：

- `entities`
- `last_scan`
- `scan_hz`
- `process_hz`
- `dma_reads` 增速
- `fail` 增速和失败率
- `latency_us avg/max`
- `SLOW_FRAME` 数量和 per-callsite breakdown
- `/api/entities` 数量、目标坐标是否稳定
- `/api/local` 是否能持续发布本地实体

建议把基线保存到一个新文件，例如：

```text
Unleashed\docs\dma-read-frequency-baseline-YYYYMMDD-HHMM.md
```

### 2. 找出最贵的读路径

当前已有 `Diagnostics::DmaCallsite`：

- `EntityScan`
- `EntityDecrypt`
- `ViewMatrix`
- `BoneChain`
- `KeyState`
- `Aimbot`
- `RenderCanvas`

慢帧日志已经会输出 100ms 窗口 per-callsite 读数和 max latency，但粒度还不够定位 `entity_thread` 内部哪个阶段最贵。

优先考虑加轻量诊断，不要先改业务逻辑：

- 在 `entity_thread()` 内围绕几个大阶段统计本轮 read 数和耗时。
- 关注阶段：
  - raw pair 读取/组件 decrypt
  - component cache refresh
  - velocity/transform/position
  - health refresh
  - hero refresh
  - bone/skeleton refresh
  - visibility
  - skill/slow fields
  - local selection
- 日志要节流，比如每 5 秒或 pipeline debug 开启时输出。

如果已有计数不容易拿到，可以先增加 `Diagnostics` 的 scoped counter 或本地 per-cycle stopwatch。不要在每个实体每个字段打日志，live 下会污染性能。

### 3. 优先做小范围优化

优先级从高到低：

1. 减少失败读和重复读。
2. 把同一实体同一周期内连续/可预知的小读合并成 `read_range`。
3. 对跨实体同类小读使用 scatter 批量读。
4. 把低价值字段降频，保持位置/关键骨骼/可见性/血量高频。
5. 对 local/当前目标做窄快路径，但不要让 aim/overlay 各自重读完整实体。

DMA 层已有 scatter API：

- `Memory::CreateScatterHandle()`
- `Memory::AddScatterReadRequest(...)`
- `Memory::ExecuteReadScatter(...)`
- `Memory::CloseScatterHandle(...)`

实现 scatter 时注意：

- 先挑一类边界清楚的读做实验，不要一次改完整 `entity_thread`。
- 每个 request 的 buffer 生命周期必须覆盖 `ExecuteReadScatter()`。
- 执行失败时保持旧路径或安全降级，不要发布半初始化实体。
- 成功后仍要保持 `OW::entities` 的发布语义不变。

可能的第一批优化候选：

- 高频 position/velocity/transform 读取。
- health 结构读取，如果现在每实体每轮都读且失败率高，需要确认是否能降频或 batch。
- visibility 读取，如果路径很散，考虑 batch 或确认是否每轮必要。
- bone/skeleton 读取，如果每轮大量小读，优先定位是否可缓存骨骼指针、只刷新关键骨点。

### 4. 不建议作为第一步的改法

这些看起来诱人，但风险较高：

- 新开很多实体 worker，把字段分散读。
- 让 aim 线程独立维护一套实体数据。
- 大幅提高 `entity_scan_thread` 频率。
- 大规模重构 `c_entity` 或发布模型。
- 用 render 帧率驱动 DMA 读取。

## 成功标准

最低完成标准：

- `.\build-release.ps1` 成功。
- 没有破坏 snapshot 消费模型。
- live 环境下能重新连接并发布实体。
- 有优化前/后数据对比。
- 有明确结论：提升了什么、没提升什么、下一步是什么。

建议成功标准：

- 在同等 live 场景下，`process_hz` 中位数提升至少 25%-30%，或者 `dma_reads/s` 降低至少 20% 且 `process_hz` 不下降。
- `entities` 和 `/api/entities` 稳定，不出现大量丢实体。
- `/api/local` 持续有本地实体。
- `SLOW_FRAME` 数量不增加，最好下降。
- DMA fail rate 不上升，最好下降。
- aim/overlay 不出现明显延迟或错位。

如果无法达到数值提升，也可以接受以下结果：

- 明确证明瓶颈主要来自硬件/VMM 延迟或某个不可安全改动路径。
- 提供阶段级读数证据。
- 留下最小可执行的下一步实验方案。

## 推荐最终报告格式

请在完成后输出：

```text
结论：
- 是否提升读取有效频率：
- 主要优化点：
- 是否保持架构边界：

构建：
- build-release.ps1：
- 其他测试/自测：

优化前基线：
- 时间：
- process_hz：
- scan_hz：
- dma_reads/s：
- fail rate：
- latency avg/max：
- SLOW_FRAME：
- entities/local：

优化后结果：
- 时间：
- process_hz：
- scan_hz：
- dma_reads/s：
- fail rate：
- latency avg/max：
- SLOW_FRAME：
- entities/local：

改动文件：
- ...

风险和回滚：
- ...

下一步：
- ...
```

## 代码参考点

入口和线程启动：

- `Unleashed/src/main.cpp`
- 搜索 `StartBackgroundThreads`
- 搜索 `std::thread(viewmatrix_thread)`
- 搜索 `std::thread(entity_scan_thread)`
- 搜索 `std::thread(entity_thread)`
- 搜索 `std::thread(aimbot_thread)`

实体 pipeline：

- `Unleashed/include/Game/Overwatch.hpp`
- 搜索 `entity_scan_thread`
- 搜索 `entity_thread`
- 搜索 `kEntityProcessIntervalMs`
- 搜索 `kEntitySlowFieldIntervalMs`
- 搜索 `publishRosterSnapshot`
- 搜索 `OW::entities = std::move`

快照消费：

- `Unleashed/include/Game/Target.hpp`
- 搜索 `SnapshotEntities`
- 搜索 `SnapshotLocalEntity`
- 搜索 `GetVector3`

DMA 读取：

- `Unleashed/src/Memory/Memory.cpp`
- 搜索 `Memory::Read`
- 搜索 `CreateScatterHandle`
- 搜索 `AddScatterReadRequest`
- 搜索 `ExecuteReadScatter`

诊断：

- `Unleashed/include/Utils/Diagnostics.hpp`
- `Unleashed/src/Utils/Diagnostics.cpp`
- 搜索 `DmaCallsite`
- 搜索 `RecordDmaRead`
- 搜索 `DumpStatus`
- 搜索 `RecordFrameTiming`
- 搜索 `GetDmaWindowStats`

TestServer：

- `Unleashed/src/Utils/TestServer.cpp`
- 搜索 `/api/health`
- 搜索 `/api/entities`
- 搜索 `/api/local`

## 当前判断

当前最可能的有效方向是：

> 保持一个实体生产者管线，减少它每轮对 DMA 的小随机读；把已知实体动态字段做批量/分层刷新；让 aim、overlay、test server 继续消费同一份 snapshot。

不要把“提升读取频率”理解成“开更多线程并行读字段”。在 UN-DMA 里，那通常会增加随机读和失败率，导致 `process_hz` 更低或数据更抖。
