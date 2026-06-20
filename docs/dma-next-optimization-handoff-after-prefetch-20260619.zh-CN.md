# DMA 读取优化下一棒交接：Prefetch 实验之后

更新时间：2026-06-19
工作目录：`D:\Desktop\SenseZen\ClaudeCodexCoding`
目标项目：`D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed`

## 给新线程的目标模式建议

建议目标设成：

> 在 `D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed` 中，接续当前已完成的 scatter hot fields 优化和 `VMMDLL_MemPrefetchPages` live 探针结论；不要回滚既有改动，不再把 prefetch 作为默认优化方向。基于当前 live 环境重新建立 30-60 秒基线，继续定位 `entity_thread` 的 DMA 失败源和高成本字段，优先做 scatter 分批读取扩展、结构/页级合并读、字段分层缓存或失败读降噪实验；保持 `entity_scan_thread -> entity_thread -> snapshot consumers` 架构不变，完成 `.\build-release.ps1`、ctest 和 live TestServer 对比验证，输出优化前后的 `process_hz`、`scan_hz`、`dma_reads/s`、失败率、延迟、`SLOW_FRAME`、实体/local 发布稳定性，以及是否值得保留该实验。

这个目标的关键点：

- 继承现状，不从零开始。
- Prefetch 已验证为“可调用但不适合默认启用”，只保留诊断开关。
- 下一步主要看 scatter/结构合并/字段分层/失败源定位。
- 优化完必须自己用 live 数据评估，不只给代码 diff。

## 当前局面一句话

刚哥已经完成两轮关键工作：

1. `scatter + hot fields` 已经有效提高 `entity_thread` 的有效处理频率。
2. `VMMDLL_MemPrefetchPages` 已经 live 验证过，不应默认开启，因为它没有提升 `process_hz`，还显著增加 `SLOW_FRAME`。

所以新线程不要再重复证明 prefetch；应该接着做更细的 scatter/结构合并/字段分层和失败源定位。

## 必读文档

请先按顺序读：

1. `Unleashed/docs/dma-read-frequency-optimization-report-20260619-1111.zh-CN.md`
2. `Unleashed/docs/dma-reference-research-and-optimization-report-20260619-1220.zh-CN.md`
3. `Unleashed/docs/dma-read-frequency-optimization-handoff.zh-CN.md`
4. `Unleashed/docs/dma-reference-research-and-optimization-handoff.zh-CN.md`

其中前两份是已经完成的实验报告，后两份是早先的工作背景。

## 当前工作树状态

当前工作树里有刚哥已经改过的源码和新增报告。不要回滚这些改动。

截至本交接生成时，`git status --short` 中相关项是：

```text
 M include/Game/Overwatch.hpp
 M include/Memory/Memory.h
 M include/Utils/Diagnostics.hpp
 M src/Memory/Memory.cpp
 M src/Utils/Diagnostics.cpp
?? docs/dma-reference-research-and-optimization-handoff.zh-CN.md
?? docs/dma-reference-research-and-optimization-report-20260619-1220.zh-CN.md
```

这些改动包含：

- scatter request 的 per-item bytes-read 支持。
- `entity_thread` 内 hot fields scatter 读取。
- `Memory::PrefetchPages()` 封装。
- `Diagnostics::DmaCallsite::EntityPrefetch`。
- `UNLEASHED_DMA_PREFETCH_HOT_PAGES` 和 `UNLEASHED_DMA_PREFETCH_MAX_PAGES` 诊断开关。

注意：prefetch 代码存在是为了诊断，不代表默认启用。

## 已完成实验 1：scatter hot fields

报告：

- `docs/dma-read-frequency-optimization-report-20260619-1111.zh-CN.md`

结论：

- 保持 `scan -> process -> snapshot` 架构。
- render/UI/TestServer 仍消费发布后的 snapshot。
- 每 process cycle DMA 请求量约 `434.4 -> 332.1 reads/cycle`，下降约 `23.6%`。
- `process_hz` 平均值约 `13.61 -> 18.68`，提升约 `37.3%`。
- `dma_reads/s` 基本持平，因为 process cycle 增多。
- `fail rate` 约 `2.65% -> 3.25%`，没有改善。
- 每 process cycle 失败读约 `11.5 -> 10.8`，略有下降。
- 两轮采样 `SLOW_FRAME=0`。
- 优化后没有 `Scatter read failed` 日志。

已改动重点：

- `include/Memory/Memory.h`
  - scatter request 增加可选 `DWORD* bytesRead` 输出。
- `src/Memory/Memory.cpp`
  - 透传 `VMMDLL_Scatter_PrepareEx(..., bytesRead)`，提交前清零计数。
- `include/Game/Overwatch.hpp`
  - `ComponentBaseCache` 缓存稳定 `matchId`。
  - 每个 entity process cycle 复用一个 scatter handle。
  - 已知实体同一轮的 `velocity_compo_t`、`health_compo_t`、`hero_compo_t` 热字段合并到 scatter。
  - 单字段未读满或 scatter 不可用时回退原 `SDK->read_range`，避免发布半初始化实体。

下一线程应保留这条主线，继续向更多高成本字段扩展，但一次只做一类字段，必须保留 per-item read bytes 检查。

## 已完成实验 2：MemPrefetchPages hot pages

报告：

- `docs/dma-reference-research-and-optimization-report-20260619-1220.zh-CN.md`

测试命令：

```powershell
.\build\Release\Unleashed.exe --test-server --test-server-port 19550
```

测试对象：

```text
Connected to Overwatch.exe (cn/ne 150818)
```

核心数据：

| 场景 | process_hz | scan_hz | dma_reads/s | 失败率 | 延迟 avg/max | SLOW_FRAME | 实体发布 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| prefetch OFF | 17.63 avg | 0.36 | 6300.4 | 7.96% | 347 us / 159295 us | 0 | `/api/entities` 6, Alive 6, Visible 6 |
| prefetch ON | 17.33 avg | 0.35 | 6287.5 | 7.80% | 361 us / 165611 us | 116 | `/api/entities` 6, Alive 6, Visible 6 |

prefetch ON 日志确认：

```text
[DMA-PREFETCH] hot_pages=103 ok=1 max_pages=128 note=direct_reads_use_nocache.
```

判定：

- `VMMDLL_MemPrefetchPages` 可调用。
- 当前本地 `Memory::Read` 和 scatter handle 使用 `VMMDLL_FLAG_NOCACHE`。
- prefetch 没有稳定改善 `process_hz`、失败率或延迟。
- prefetch ON 明显增加 `SLOW_FRAME`。
- 默认应关闭，只保留环境变量诊断开关：

```text
UNLEASHED_DMA_PREFETCH_HOT_PAGES=1
UNLEASHED_DMA_PREFETCH_MAX_PAGES=128
```

新线程不要把 prefetch 当主线继续优化。除非用户明确要求，否则只把它作为对照/诊断开关。

## 当前推荐下一步

优先级从高到低：

### 1. 定位 fail rate 来源

当前 `process_hz` 已经提升，但 fail rate 没有改善，并且 prefetch 实验里 fail rate 到了约 8% 的 live 量级。

下一步应拆分 `EntityDecrypt` 内部失败来源：

- component refresh / component decrypt
- velocity
- health
- hero
- visibility
- team
- transform
- bone/skeleton block
- rotation/angle
- skill/slow fields

要求：

- 每 5 秒汇总一次即可。
- 不要每实体每字段打日志。
- 输出要能回答：失败主要来自哪个字段/阶段，是否是可降频/可缓存/可合并读的路径。

### 2. 扩展 scatter，但一次只扩一类字段

候选顺序：

1. visibility
2. transform / position fallback
3. team flags
4. rotation / angle
5. skeleton block 读取路径的失败统计或批量化改良

原则：

- 一次只选一类字段。
- 保留旧路径回退。
- 每项必须检查 bytes read。
- partial read 不发布半初始化数据。
- 记录 item count、partial count、fallback count。

### 3. 结构/页级合并读

适合把同一 base 附近多个 `RPM<T>` 合成一个 `read_range`，读到本地结构后解析。

优先找这些模式：

- 同一 base 连续/近距离字段。
- 每个 entity 每轮都读的小字段。
- 当前多次 `SDK->RPM<T>` 连续出现的代码段。

不要为了“合并”读取巨大无关范围；跨页大块读可能反而增加 partial/fail。

### 4. 字段分层缓存

当前已经有：

```cpp
kEntityProcessIntervalMs = 16
kEntityHealthIntervalMs = kEntityProcessIntervalMs
kEntityHeroIntervalMs = 50
kEntitySlowFieldIntervalMs = 500
kEntityLiveComponentRefreshMs = 2000
kEntityDeadComponentRefreshMs = 250
```

可以检查：

- 哪些字段其实不需要每 process cycle 读。
- health 是否必须所有实体每 cycle 都读，还是可对非目标/远距离实体降低频率。
- hero/team/skill/name 是否已经完全被 slow/hero interval 约束。
- visibility 是否能高频但批量，或根据 target/overlay 需求分层。

不要牺牲 aim/overlay 必需的 hot fields：位置、关键骨点、可见性、本地实体、当前目标相关字段。

## 新线程启动步骤

### 1. 保护现状

先看状态，不要 reset：

```powershell
cd D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed
git status --short
git diff --stat
```

### 2. 读报告

```powershell
Get-Content .\docs\dma-read-frequency-optimization-report-20260619-1111.zh-CN.md
Get-Content .\docs\dma-reference-research-and-optimization-report-20260619-1220.zh-CN.md
```

### 3. 建立新的 live 基线

如果用户保持 live 环境开着，先不要改代码，采 30-60 秒：

```powershell
Select-String -Path .\build\Release\unleashed_diag.log -Pattern "STATUS entities=|SLOW_FRAME" |
  Select-Object -Last 100
```

如果 test server 开着：

```powershell
Invoke-RestMethod http://127.0.0.1:19550/api/health
Invoke-RestMethod http://127.0.0.1:19550/api/entities
Invoke-RestMethod http://127.0.0.1:19550/api/local
```

不要硬编码端口。如果 `19550` 不通，先从运行参数、日志或用户说明确认实际端口。

### 4. 改一个点，测一轮

每次只做一个实验：

- fail source instrumentation
- scatter 扩展一类字段
- read_range 合并一处结构
- 某类字段降频/缓存

每次实验都要保留：

- before 窗口
- after 窗口
- build/test 结果
- live API 结果
- 是否值得保留

## 构建与验证

规范构建：

```powershell
cd D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed
.\build-release.ps1
```

ctest：

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build -C Release --output-on-failure
```

如果 `ctest` 不在 PATH，不要停住；用上面的完整路径。

Live 验证至少记录：

- `process_hz`
- `scan_hz`
- `dma_reads/s`
- fail rate
- `latency_us avg/max`
- `SLOW_FRAME`
- `/api/entities`
- `/api/local`
- 是否残留测试进程

## 禁区

不要做：

- 不要回滚刚哥的改动。
- 不要默认开启 `UNLEASHED_DMA_PREFETCH_HOT_PAGES`。
- 不要继续把 prefetch 当主线优化。
- 不要简单调小 `kEntityProcessIntervalMs`。
- 不要新开多个 worker 线程分别读 health/team/bone/visibility。
- 不要让 aim/overlay/test server 直接读完整实体数据。
- 不要去重写整体 pipeline。
- 不要一次 scatter 化所有字段。
- 不要忽略 partial read。
- 不要只报平均值，必须看 max latency 和 `SLOW_FRAME`。

## 成功标准

最低成功：

- build 通过。
- ctest 通过或明确说明无法运行原因。
- live 能发布 entities/local。
- 有 before/after 对比。
- 没有破坏 `scan -> process -> snapshot`。
- 能明确说这个实验保留还是回滚。

理想成功：

- `process_hz` 中位数继续提升，或
- 每 process cycle read 数继续下降，或
- fail rate 明显下降，或
- `latency_us max` / `SLOW_FRAME` 明显下降，且
- `/api/entities` 与 `/api/local` 稳定。

如果性能没有提升，也可以算成功：

- 定位出主要失败来源。
- 证明某条路径不值得继续做。
- 留下下一步最小实验。

## 推荐最终报告格式

请新增报告文件，例如：

```text
docs/dma-next-optimization-report-YYYYMMDD-HHMM.zh-CN.md
```

报告格式：

```text
# DMA 下一轮读取优化报告

## 结论
- 本轮做了什么：
- 是否保留：
- 为什么：

## 继承基线
- scatter hot fields 结论：
- prefetch 结论：

## 本轮改动
- 文件：
- 行为：
- 回退机制：

## 优化前 live 基线
- 时间窗口：
- process_hz：
- scan_hz：
- dma_reads/s：
- fail rate：
- latency avg/max：
- SLOW_FRAME：
- entities/local：

## 优化后 live 结果
- 时间窗口：
- process_hz：
- scan_hz：
- dma_reads/s：
- fail rate：
- latency avg/max：
- SLOW_FRAME：
- entities/local：

## 构建与测试
- build-release.ps1：
- config-check：
- ctest：
- live TestServer：

## 风险和下一步
- 风险：
- 是否建议默认开启：
- 下一步：
```

## 一句话给新线程

当前不是“证明 prefetch 有没有用”的阶段了；这件事已经做完，结论是默认不用。下一步要把刚哥已经证明有效的 scatter/合并读路线继续往高成本字段推进，同时把 fail rate 的来源拆清楚。
