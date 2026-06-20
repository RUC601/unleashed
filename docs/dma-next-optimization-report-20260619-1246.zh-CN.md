# DMA 下一轮读取优化报告

时间：2026-06-19 12:33-12:44 PDT

## 结论

- 本轮做了两个小实验：
  - 为 `entity_thread` 增加默认 5 秒一次的字段级 DMA 窗口汇总。
  - 将 CN/NE `visibility` raw bool 读取加入已有 hot-field scatter，保留单读回退和 per-item bytes 检查。
- 是否保留：
  - 建议保留本轮代码，但不要把它描述成显著性能优化。
  - `visibility` scatter 在 live 中 2232/2232 命中，0 partial，0 fallback，0 read_fail，0 anomaly，低风险。
  - 端到端指标只小幅变化：`dma_reads/s` 和失败率略降，`process_hz` 基本持平，`SLOW_FRAME` 没改善。
- 主要定位结论：
  - 当前 live 的 hot fields 不是 DMA 失败源。velocity/health/hero/visibility scatter 全部无 partial/fallback/read fail。
  - component header/link header 快照读取本身也没有 read fail。
  - 高成本面更像是 component cache miss、link/component decrypt 后的 `link_base` 缺失，以及 entity_thread 之外的 EntityScan/ViewMatrix/Aimbot/RenderCanvas 读延迟尖峰。

## 继承基线

- scatter hot fields：上一轮把每 process cycle 读请求约 `434.4 -> 332.1`，`process_hz` 约 `13.61 -> 18.68`。
- prefetch：`VMMDLL_MemPrefetchPages` 已验证可调用，但默认不启用；当前路径仍不把 prefetch 作为优化方向。

## 本轮改动

- 文件：
  - `include/Game/Overwatch.hpp`
- 行为：
  - `EntityHotFieldReads` 新增 CN/NE visibility raw qword。
  - `readHotFields()` 在已有 scatter execute 中附带 visibility value address。
  - 每 5 秒输出 `[DMA-FIELD]` 汇总：component cache/header、velocity、health、hero、visibility、team/skill、bone、link_base 等。
- 回退机制：
  - visibility scatter 只在 CN/NE 且 `VisibilityValueOffset` 有效时启用。
  - scatter 未读满时回退 `SDK->read_range(VisBase + VisibilityValueOffset)`。
  - partial/read fail 不发布半初始化 visibility；失败时按 invisible 处理，保持旧路径的保守行为。

## 优化前 live 基线

- 时间窗口：`12:33:07-12:34:02`，55 秒，12 个 STATUS 样本。
- `process_hz`：avg `12.69`，min `2.4`，max `40.4`。
- `scan_hz`：avg `0.21`。
- `dma_reads/s`：`5887.2`。
- `dma_ok/s`：`5789.0`。
- `dma_fail/s`：`98.2`。
- fail rate：`1.67%`。
- latency avg/max：`625 us / 161368 us`。
- `SLOW_FRAME`：`1302`。
- TestServer：`/api/entities` 发布 9 个，Alive 9，Visible 3；`/api/local` 发布 Ashe，250/250，alive。

## 优化后 live 结果

- 时间窗口：`12:43:27-12:44:22`，55 秒，12 个 STATUS 样本。
- `process_hz`：avg `12.78`，min `2.4`，max `40.4`。
- `scan_hz`：avg `0.22`。
- `dma_reads/s`：`5850.6`。
- `dma_ok/s`：`5755.9`。
- `dma_fail/s`：`94.7`。
- fail rate：`1.62%`。
- latency avg/max：`626 us / 166376 us`。
- `SLOW_FRAME`：`1313`。
- TestServer：`/api/health` connected to `Overwatch.exe (cn/ne 150818)`；`/api/entities` 发布 10 个，Alive 10，Visible 2；`/api/local` 发布 Ashe，250/250，alive。

## 字段级定位

最终 live 聚合了 11 个 `[DMA-FIELD]` 窗口：

- process cycles：`139`，raw `3779`，validated `2819`，published `2454`。
- hot scatter execute：`2940`，execute fail `0`。
- component cache：hit `1357`，miss `1870`。
- component header：read `1870`，fail `0`。
- link header：read `1107`，fail `0`。
- velocity：req `2907`，scatter_hit `2907`，partial/fallback/fail 全部 `0`。
- health：req `2819`，scatter_hit `2819`，partial/fallback/read_fail/layout_fail 全部 `0`，missing `323`。
- hero：req `2232`，scatter_hit `2232`，partial/fallback/read_fail/fallback_fail 全部 `0`，missing `587`。
- visibility：req `2232`，scatter_hit `2232`，partial/fallback/read_fail/missing/anomaly 全部 `0`。
- entity_fail：link_base `958`，name_unknown `0`。
- team/skill refresh：各 `2163`。
- bones：candidates `2232`，any/head valid `2191/2191`。

## 构建与测试

- `.\build-release.ps1`：通过。第一次重建遇到 `LNK1104`，原因是旧 `Unleashed.exe` 进程占用输出文件；停止进程后重跑通过。
- `.\build\Release\Unleashed.exe --config-check`：通过。
- `ctest.exe --test-dir build -C Release --output-on-failure`：9/9 通过。
- live TestServer：通过，测试进程已清理，无残留 `Unleashed.exe`。

## 风险和下一步

- 风险：
  - 本轮 hidden TestServer 运行下 render/present 触发大量 `SLOW_FRAME`，不宜把 `SLOW_FRAME` 完全归因给 entity_thread。
  - visibility scatter 收益很小，当前证据不足以证明它能稳定提高 `process_hz`。
- 是否建议默认保留：
  - 建议保留字段级汇总和 CN/NE visibility scatter。理由是低风险、无 partial、无回退、可减少一类单字段高频读，并能继续提供定位证据。
  - 不建议把它包装成主优化收益点。
- 下一步：
  - 优先看 component cache miss 为什么长期高于 cache hit，以及 `link_base` 缺失是否来自无效 raw entity、LinkParent/component parent 混入或 refresh interval 过于激进。
  - 对 team/skill slow refresh 做结构合并或进一步降噪；当前每 5 秒窗口里 team/skill refresh 各约 180-224 次。
  - 分开统计 EntityScan/ViewMatrix/Aimbot/RenderCanvas 的 fail rate 和延迟尖峰；本轮 global fail 不来自 hot field scatter。
