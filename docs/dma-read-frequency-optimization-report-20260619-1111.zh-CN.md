# DMA 读取频率优化报告

时间：2026-06-19 11:01-11:10 PDT

## 结论

- 已保持 `scan -> process -> snapshot` 架构；render/UI/TestServer 仍消费已发布 snapshot。
- 已知实体 `entity_thread` 热路径的每 process cycle DMA 请求量下降：约 `434.4 -> 332.1 reads/cycle`，下降约 `23.6%`。
- `process_hz` 平均值提升：约 `13.61 -> 18.68`，提升约 `37.3%`。
- `dma_reads/s` 基本持平：约 `5944.5 -> 5968.2`。原因是 process cycle 明显增多；按每周期请求量看，优化命中了高频小请求路径。
- `fail rate` 从约 `2.65%` 到 `3.25%`，没有改善；但每 process cycle 失败读约 `11.5 -> 10.8`，略有下降。这里仍需下一轮针对失败源定位。
- 两轮 live 采样都没有 `SLOW_FRAME`；优化后没有 `Scatter read failed` 日志。

## 改动

- `include/Memory/Memory.h`
  - 为 scatter request 增加可选 `DWORD* bytesRead` 输出。
- `src/Memory/Memory.cpp`
  - 透传 `VMMDLL_Scatter_PrepareEx(..., bytesRead)`，并在提交前清零计数。
- `include/Game/Overwatch.hpp`
  - 在 `ComponentBaseCache` 中缓存稳定的 `matchId`，避免每轮每 raw entity 重读 `Entity_MatchId`。
  - 在每个 entity process cycle 中复用一个 scatter handle。
  - 将已知实体同一轮的 `velocity_compo_t`、`health_compo_t`、`hero_compo_t` 热字段读合并到 scatter。
  - 单字段未读满或 scatter 不可用时回退原 `SDK->read_range` 路径，避免发布半初始化实体。

## 构建和自测

- `.\build-release.ps1`：成功。
- `.\build\Release\Unleashed.exe --config-check`：成功。
- `ctest.exe --test-dir build -C Release --output-on-failure`：9/9 通过。

## 优化前基线

- 时间窗口：`11:01:54 - 11:03:04`，70 秒，15 个 STATUS 样本。
- `process_hz`：avg `13.61`，min `8.1`，max `17.7`。
- `scan_hz`：avg `0.13`。
- 实体数量：`7 - 10`。
- `dma_reads/s`：`5944.5`。
- `dma_ok/s`：`5786.8`。
- `dma_fail/s`：`157.7`。
- `fail rate`：`2.65%`。
- `latency_us avg/max`：最后样本 `512 / 262103`。
- 每 process cycle 读请求：约 `434.4`。
- `SLOW_FRAME`：0。
- TestServer：`/api/health` connected；`/api/entities` 可发布敌方实体；`/api/local` 持续发布本地 Freja。

## 优化后结果

- 时间窗口：`11:09:30 - 11:10:40`，70 秒，15 个 STATUS 样本。
- `process_hz`：avg `18.68`，min `10.6`，max `24.1`。
- `scan_hz`：avg `0.19`。
- 实体数量：`7 - 10`。
- `dma_reads/s`：`5968.2`。
- `dma_ok/s`：`5774.3`。
- `dma_fail/s`：`193.9`。
- `fail rate`：`3.25%`。
- `latency_us avg/max`：最后样本 `491 / 133796`。
- 每 process cycle 读请求：约 `332.1`。
- `SLOW_FRAME`：0。
- `Scatter read failed`：0。
- TestServer `/api/entities` 摘要：6 个 enemy，6 alive，6 visible，距离约 `20.211 - 41.4937m`。
- TestServer `/api/local` 摘要：Freja，`225/225`，alive，player controller `0x213BB52C000`。

## 风险和下一步

- 当前 `dma_reads/s` 没下降，是因为更高的 process 频率消化了节省出来的请求预算；这符合“提高有效刷新率”的目标，但不是总带宽下降。
- `fail rate` 未改善，下一步应在 `EntityDecrypt` 内继续按字段/阶段拆分失败来源，尤其是 component refresh、bone block 和 visibility 路径。
- 如果下一轮目标是降低总 `dma_reads/s`，可以在保持本次 scatter 的基础上，对 health/hero/skill 做更明确的分层刷新，而不是继续加并行 worker。
