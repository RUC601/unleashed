# DMA 链路参考资料与读取优化交接文档

更新时间：2026-06-19
工作目录：`D:\Desktop\SenseZen\ClaudeCodexCoding`
目标项目：`D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed`

## 给新线程的目标模式建议

建议目标设成：

> 在 `D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed` 中，基于官方 MemProcFS/LeechCore/memflow 资料和本地既有代码，研究 DMA 链路下高频小随机读的性能约束，形成可落地的优化实验；在 live 环境开启时建立当前读取基线，优先验证 `VMMDLL_MemPrefetchPages`、scatter 分批读取、结构/页级合并读取和字段分层缓存对 `entity_thread` 的影响；保持现有 `entity_scan_thread -> entity_thread -> snapshot consumers` 架构不变，完成构建和 live 对比评估，输出优化前后的 `process_hz`、`scan_hz`、`dma_reads/s`、失败率、延迟、`SLOW_FRAME`、实体发布稳定性和下一步结论。

这个目标比“提高读取频率”更好，因为它要求：

- 先读资料和当前代码，不凭感觉调 Sleep。
- 先建立 live 基线，再做小范围实验。
- 先证明 MemProcFS/memflow 资料能对应到我们的路径，再改代码。
- 优化后必须自己评估，不能只说“理论上会更快”。

## 任务背景

用户发现当前参考资料特别少：GitHub 上容易搜到很多 OW external 项目，但这些普通 external 读内存的成本和我们的 UN-DMA 链路不同。普通 external 通常在同机进程中读内存，系统调用/共享状态成本相对低；我们的路径是外部 DX11 overlay 通过 LeechCore/MemProcFS/VMM 访问硬件 DMA 设备，容易被大量小随机读、设备串行访问、VMM 内部 refresh、页表/TLB 缓存刷新和 partial read 影响。

因此，这次不要继续用 `OW external` 作为主要关键词。更有效的资料方向是：

- `MemProcFS FAQ Timing`
- `VMMDLL_Scatter_*`
- `VMMDLL_MemPrefetchPages`
- `LeechCore FPGA latency`
- `memflow read_raw_iter`
- `memflow batchable IO`
- `KVM/QEMU memory reader`
- `process_vm_readv batch read`

已有一个纯优化交接文档：

- `Unleashed/docs/dma-read-frequency-optimization-handoff.zh-CN.md`

本文档补充“参考资料如何找、怎么看、怎么转化成实验”。

## 当前项目事实

关键源码：

- `Unleashed/include/Game/Overwatch.hpp`
- `Unleashed/include/Game/Target.hpp`
- `Unleashed/src/Memory/Memory.cpp`
- `Unleashed/include/Memory/Memory.h`
- `Unleashed/include/Utils/Diagnostics.hpp`
- `Unleashed/src/Utils/Diagnostics.cpp`
- `Unleashed/src/Utils/TestServer.cpp`
- `Unleashed/src/main.cpp`

当前架构已经接近 Rigel 的“线程按职责分工 + 共享快照”，但 DMA 链路更敏感：

- `entity_scan_thread()`：扫描 raw entity pair，写入 `OW::ow_entities_scan`。
- `entity_thread()`：读取/解密/刷新实体字段，发布 `OW::entities`、`OW::hp_dy_entities`、`OW::local_entity`。
- `viewmatrix_thread()`：少量高频读取矩阵。
- `aimbot_thread()`：主要消费 `TargetingDetail::SnapshotEntities()` 和 `SnapshotLocalEntity()`，只额外读少量本地即时字段。
- render/overlay/test server：应该消费 snapshot，不应该直接发起实体 DMA 读。

当前频率常量在 `Overwatch.hpp`：

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

最近一次观察到的 live 日志基线大致是：

- `entities=7`
- `last_scan=22`
- `scan_hz=0.1-0.2`
- `process_hz=11.7-16.8`
- `dma_reads` 大约 5.8k-6.2k reads/s
- `latency_us[min/avg/max]` 大约 `0/463-467/190734`
- 出现过 `SLOW_FRAME total=52.7ms`
- 慢帧 100ms 窗口中有过约 600 次 DMA read，主要来自 `EntityScan`、`EntityDecrypt`、`ViewMatrix`、`Aimbot`

这些数字只能作为旧基线参考。新线程必须重新从当前 live 环境取数据。

## 官方资料阅读清单

### 1. MemProcFS FAQ Timing

URL:

- https://github.com/ufrisk/MemProcFS/wiki/FAQ_Timing

重点：

- FPGA 硬件采集时，同一时刻只有一个线程能访问设备。
- 其他线程会排队，可能造成短暂延迟尖峰。
- MemProcFS 默认会做内部 refresh，这些 refresh 也需要读 FPGA。
- 内部 refresh 进行时，DMA read 可能被延迟。
- 默认 refresh 有 Memory/TLB/FAST/MEDIUM/SLOW 几类周期。
- 可以调整 refresh 行为，但完全禁用 refresh 需要谨慎，否则长时间运行会错过新进程/新分配等变化。

转化到本项目的结论：

- 不要通过“开很多读线程”解决大量小随机读。
- 需要关注延迟尖峰，不只看平均延迟。
- 评估窗口至少 30-60 秒，因为 refresh 会周期性影响数据。
- 如果要实验 refresh 参数，必须单独隔离实验，不能和 scatter/prefetch 同时改。

### 2. MemProcFS C API - Scatter Read

URL:

- https://github.com/ufrisk/MemProcFS/wiki/API_C

重点：

Scatter 流程是：

1. `VMMDLL_Scatter_Initialize`
2. 多次 `VMMDLL_Scatter_Prepare` 或 `VMMDLL_Scatter_PrepareEx`
3. `VMMDLL_Scatter_ExecuteRead`
4. 如果用了 `Prepare`，再 `Scatter_Read`
5. `VMMDLL_Scatter_Clear` 复用，或 `VMMDLL_Scatter_CloseHandle` 释放

`PrepareEx` 可以直接把内容写入传入的 buffer，并可提供 `pcbRead`。

转化到本项目的结论：

- scatter 是减少设备往返的官方路径。
- 但必须检查每个 request 的 read bytes，不能只看 `ExecuteRead` 总返回值。
- buffer 生命周期要覆盖 `ExecuteRead` 和后续解析。
- 先做一类字段的小实验，别一次改完整 `entity_thread`。
- 推荐分批测试 32/64/128 个 request，而不是一上来塞几百个。

### 3. MemProcFS C API - MemPrefetchPages

URL:

- https://github.com/ufrisk/MemProcFS/wiki/API_C

重点：

同一 API 页面里有：

```cpp
BOOL VMMDLL_MemPrefetchPages(
    VMM_HANDLE hVMM,
    DWORD dwPID,
    PULONG64 pPrefetchAddresses,
    DWORD cPrefetchAddresses
);
```

转化到本项目的结论：

- 可以实验性把“已知实体的 hot pages”预取到 MemProcFS 本地 cache。
- 第一阶段可以只加 prefetch，不改后续字段读取路径，这样实验隔离最干净。
- 预取地址应按页去重。
- 预取是否有效要用 live 数据证明：`process_hz`、`dma_reads/s`、fail rate、latency、SLOW_FRAME。

### 4. MemProcFS issue #252 - Scatter partial read 风险

URL:

- https://github.com/ufrisk/MemProcFS/issues/252

重点：

- 有使用者报告约 300 个 scatter request 时读不全。
- 这不是可以直接套用的结论，但说明 scatter 使用必须防 partial read。

转化到本项目的结论：

- 每个 scatter item 必须记录 `pcbRead` 或等价状态。
- partial read 不应发布半初始化实体。
- 需要统计 scatter item fail/partial rate。
- 分批大小应作为实验变量。

### 5. memflow MemoryView / read_raw_iter

URL:

- https://docs.rs/memflow/latest/memflow/mem/memory_view/trait.MemoryView.html
- https://docs.rs/memflow/latest/memflow/

重点：

- memflow 的 `MemoryView` 抽象明确支持批量 IO。
- `read_raw_iter` 接收一组地址/buffer 操作。
- memflow 文档说它希望 IO 是 batchable，以获得性能。

转化到本项目的结论：

- 另一个成熟内存框架也把“批量读”作为核心性能模型。
- 这支持我们的优化方向：减少零散 `RPM<T>`，批量读后本地解析。
- 不需要引入 memflow 到当前 DMA 项目，只借鉴它的设计思想。

### 6. memflow 0.2.0 架构说明

URL:

- https://memflow.io/blog/memflow-0.2.0/

重点：

- memflow 把 connector 和 OS layer 分层。
- connector 负责物理内存访问，OS layer 建立进程/模块等抽象。

转化到本项目的结论：

- 我们也应保持后端 reader 和上层 snapshot/实体解析分层。
- 不要让 UI/aim/render 直接绑定底层 DMA 读。
- 后续 KVM/memflow 路径和当前 FPGA/MemProcFS 路径应该共享 snapshot 概念，而不是共享随机读调用。

## 本地旧研究参考

本机记忆里有过 `CodeReadingLab` 源码阅读：

- `apex_dma_kvm_pub-master`
- `blacksun-framework-master`
- FPGA DMA 对照项目

旧研究结论：

- `apex_dma_kvm_pub` 使用 memflow `qemu/kvm` connector、guest Windows process lookup 和虚拟地址 `read_raw_into`。
- 该项目有一种有用模式：先整块读实体对象到本地 buffer，再按 offset 本地取字段。
- `blacksun-framework` 是 host-side QEMU PID discovery、`info mtree`、`process_vm_readv` 和自写 MMU/NT helper。
- FPGA 路径常见依赖是 `vmm.dll`、`leechcore.dll`、`FTD3XX.dll`，读取 API 是 `VMMDLL_MemReadEx` 或 scatter。

对当前项目的借鉴：

- 不是照搬 KVM 项目，而是借鉴“后端 reader 分层 + 本地 buffer 解析 + 批量读”的形态。
- 普通 OW external 多数不适合做性能参考。
- KVM/QEMU 项目也不等于我们的 FPGA/MemProcFS 链路，但它们共同避免让多个消费者随意读内存。

如果需要复核本地旧研究，从 memory registry 里找关键词：

- `apex_dma_kvm_pub`
- `blacksun-framework`
- `read_raw_into`
- `process_vm_readv`
- `VMMDLL_MemReadEx`
- `FPGA DMA`

## 推荐搜索关键词

如果需要继续找资料，不要从 `Overwatch external` 开始，优先搜：

```text
MemProcFS FAQ Timing FPGA threads latency
VMMDLL_Scatter_PrepareEx ExecuteRead example
VMMDLL_MemPrefetchPages performance
MemProcFS scatter partial read
LeechCore FPGA latency scatter read
memflow read_raw_iter batch read
memflow MemoryView batchable IO
memflow qemu kvm read_raw_into
process_vm_readv batch memory reader
```

如果要找 GitHub 代码，优先搜：

```text
"VMMDLL_Scatter_Initialize" "VMMDLL_Scatter_PrepareEx"
"VMMDLL_MemPrefetchPages"
"read_raw_iter" "memflow"
"read_raw_into" "memflow"
"VMMDLL_MemReadEx" "FTD3XX.dll" "leechcore.dll"
```

## 实验路线

### 阶段 0：建立 live 基线

先不要改代码。

命令：

```powershell
cd D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed

Get-Process | Where-Object {
  $_.ProcessName -match 'Unleashed|overlay|imgui|leech|dma|kmbox|sense|scenario'
} | Select-Object Id,ProcessName,Path

Select-String -Path .\build\Release\unleashed_diag.log -Pattern "STATUS entities=|SLOW_FRAME" |
  Select-Object -Last 80
```

如果 test server 开着，确认真实端口后访问：

```powershell
Invoke-RestMethod http://127.0.0.1:<port>/api/health
Invoke-RestMethod http://127.0.0.1:<port>/api/entities
Invoke-RestMethod http://127.0.0.1:<port>/api/local
```

记录：

- 时间窗口
- `entities`
- `last_scan`
- `scan_hz`
- `process_hz`
- `dma_reads/s`
- fail rate
- `latency_us avg/max`
- `SLOW_FRAME` 数量和 per-callsite breakdown
- `/api/entities` 数量和坐标稳定性
- `/api/local` 是否持续可用

### 阶段 1：定位读压力

先确认 `entity_thread()` 内部哪些阶段消耗最多读。

可加轻量诊断：

- 每 5 秒汇总一次。
- 不要每实体每字段打日志。
- 区分至少这些阶段：
  - raw pair / component decrypt
  - component cache refresh
  - velocity/transform/position
  - health
  - visibility
  - hero/name/team/skill slow fields
  - bone/skeleton
  - local selection

如果已有 `DmaCallsite` 粒度不够，可以短期增加 `EntityHealth`、`EntityTransform`、`EntityBone` 之类的内部统计，但要保持改动小。

### 阶段 2：Prefetch hot pages 实验

目标：

- 不改变实体语义。
- 在 `entity_thread` 一轮开始时，对已知实体关键地址按页去重后调用 `VMMDLL_MemPrefetchPages`。
- 先验证 prefetch 单独作用。

候选地址来源：

- `ComponentParent`
- `LinkParent`
- `HealthBase`
- `VelocityBase`
- `HeroBase`
- `TeamBase`
- `VisBase`
- `BoneBase`
- transform/mesh 相关 base

注意：

- 地址必须先过滤 0/null/明显非法值。
- 以 4KB 页对齐去重。
- 控制每轮 prefetch 页数，防止把 prefetch 本身变成新瓶颈。
- 用 feature flag 或 constexpr 方便回滚。

评估：

- `process_hz` 是否上升。
- `dma_reads/s` 是否下降或不显著上升。
- fail rate 是否下降。
- `latency_us max` 和 `SLOW_FRAME` 是否改善。

### 阶段 3：一类字段 scatter 实验

目标：

- 只挑一类字段，不要一次改完整实体管线。
- 推荐候选：health 或 transform/velocity。

做法：

- 先收集一轮需要读取的地址和 buffer。
- 用 `Memory::CreateScatterHandle()`。
- 多次 `Memory::AddScatterReadRequest()` 或直接扩展支持 `pcbRead` 的 PrepareEx 包装。
- `ExecuteReadScatter()`。
- 检查每项读取结果。
- 失败项安全降级，保持旧值或丢弃该实体，不发布半初始化数据。

注意：

- 当前 `Memory::AddScatterReadRequest(...)` 没有暴露 `pcbRead`，可能需要给 scatter request 增加 per-item bytes-read 支持。
- 不要只看 scatter 总成功。
- 分批大小建议从 32 或 64 开始，逐步对比。

评估：

- 同阶段 2。
- 额外记录 scatter item count、partial count、failed item count。

### 阶段 4：结构/页级合并读

目标：

- 把同一结构附近的多个 `RPM<T>` 合并成一次 `read_range`。
- 读到本地结构后再解析字段。

适合场景：

- 同一 base 附近连续字段。
- 每轮都读的多个小字段。
- 原本多个 `RPM<T>` 只相隔几十/几百字节。

不适合场景：

- 地址链还没确认有效。
- 大块读取跨很多无关页。
- 容易触发 partial read 而难以回退。

### 阶段 5：字段分层缓存

目标：

- 高频刷新：position、velocity、关键 bone、visibility、health、local view/angle/sens。
- 中频刷新：hero id、team、skill base、rotation、reload 等。
- 低频刷新：name、static component、很少变化的映射。

当前已有：

- `kEntitySlowFieldIntervalMs = 500`
- `kEntityHeroIntervalMs = 50`
- `kEntityLiveComponentRefreshMs = 2000`
- `kEntityDeadComponentRefreshMs = 250`

先确认这些逻辑是否真正覆盖高成本字段，再决定是否调整。

## 禁区

不要做这些：

- 不要简单把 `kEntityProcessIntervalMs` 改小后宣称完成。
- 不要新增多个线程分别读取 health/team/bone/visibility。
- 不要让 aim/overlay/test server 绕过 snapshot 直接读 DMA。
- 不要让 render callback 中出现实体 DMA 读。
- 不要拿普通 OW external 项目的线程数作为性能依据。
- 不要一次改完整 `entity_thread`。
- 不要忽略 partial read。
- 不要只看平均延迟，必须看 max latency 和 SLOW_FRAME。

## 构建和验证

规范构建：

```powershell
cd D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed
.\build-release.ps1
```

如果需要跑 ctest，优先使用本机 VS CMake 自带路径：

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build -C Release --output-on-failure
```

验证顺序：

1. Build 通过。
2. 启动/复用 live 环境。
3. 读取 `unleashed_diag.log` 状态。
4. 检查 `/api/health`。
5. 检查 `/api/entities`。
6. 检查 `/api/local`。
7. 对比优化前后 30-60 秒窗口。

## 成功标准

最低成功：

- 构建通过。
- 架构边界不变。
- live 能发布实体和 local。
- 有优化前/后对比。
- 能明确说明参考资料如何支持或否定某个实验。

理想成功：

- `process_hz` 中位数提升 25%-30% 以上，或
- `dma_reads/s` 降低 20% 以上且 `process_hz` 不下降，或
- fail rate / `latency_us max` / `SLOW_FRAME` 明显下降。

如果没有性能提升，也可以算有价值：

- 证明 prefetch/scatter 在当前路径下没有收益或风险较大。
- 找到真正瓶颈阶段。
- 给出下一步最小实验。

## 推荐最终输出格式

```text
结论：
- 本轮是否提升：
- 最有效资料：
- 最有效实验：
- 保持/改变了哪些架构边界：

参考资料：
- MemProcFS FAQ Timing：
- MemProcFS Scatter API：
- MemPrefetchPages：
- memflow batch read：
- 本地 CodeReadingLab 旧研究：

优化前：
- 时间窗口：
- process_hz：
- scan_hz：
- dma_reads/s：
- fail rate：
- latency avg/max：
- SLOW_FRAME：
- entities/local：

改动：
- 文件：
- 方法：
- 回滚点：

优化后：
- 时间窗口：
- process_hz：
- scan_hz：
- dma_reads/s：
- fail rate：
- latency avg/max：
- SLOW_FRAME：
- entities/local：

判断：
- 为什么有效/无效：
- 还有什么风险：
- 下一步：
```

## 一句话方向

这次工作的核心不是找“另一个 OW 项目怎么写”，而是把 MemProcFS/memflow 这类底层资料里的共同原则落到我们的代码里：

> 少线程访问设备，批量提交读请求，本地解析 buffer，分层缓存字段，用 live 指标证明收益。
