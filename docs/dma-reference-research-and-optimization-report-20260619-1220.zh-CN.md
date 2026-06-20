# DMA 高频小随机读优化评估报告 2026-06-19 12:20

## 结论

本轮在不改变 `scan / process / snapshot` 架构的前提下，验证了官方资料里最相关的 DMA 约束，并完成 `VMMDLL_MemPrefetchPages` 的 live 探针实验。当前应保留 scatter 分批读取、结构/页级合并读取、字段分层缓存作为默认路径；`VMMDLL_MemPrefetchPages` 不应默认启用，只作为 `UNLEASHED_DMA_PREFETCH_HOT_PAGES=1` 的诊断开关保留。

原因是当前本地读路径使用 `VMMDLL_FLAG_NOCACHE`。prefetch 能成功调用并返回 ok，但不会稳定改善 entity_thread 的 `process_hz`、失败率或延迟，且本次 live 对比中 prefetch ON 明显增加 `SLOW_FRAME`。

## 官方资料要点

- MemProcFS Timing FAQ 指出 FPGA 设备一次只处理一个读取线程，其他线程排队；内部刷新也可能造成 DMA 延迟尖峰。这和本地 `SLOW_FRAME` 中多 callsite 在同一窗口争用的现象一致。
- MemProcFS C API 推荐 scatter 流程：Prepare/PrepareEx 多个地址，再 ExecuteRead，最后 Clear/Close。对高频小随机读，scatter 的收益主要来自减少跨 API/设备队列的提交碎片。
- 本地 `vendor/leechcore/vmmdll.h` 暴露 `VMMDLL_MemPrefetchPages`，但本地 `Memory::Read` 与 scatter handle 读路径使用 `VMMDLL_FLAG_NOCACHE`，会绕过常规缓存命中收益。
- LeechCore FPGA 文档里的 `readsize`、`readretry`、`tmread` 等参数说明底层链路存在固定事务粒度和超时/重试成本，因此结构合并读取、页级合并和字段分层缓存比单字段随机读更稳定。
- memflow 的 memory interface 也强调可 batch、可拆分的内存访问模型，和本地 scatter/结构合并路线一致。

参考：
- https://github.com/ufrisk/MemProcFS/wiki/FAQ_Timing
- https://github.com/ufrisk/MemProcFS/wiki/API_C
- https://github.com/ufrisk/LeechCore
- https://github.com/ufrisk/LeechCore/wiki/Device_FPGA
- https://docs.rs/memflow/latest/memflow/
- https://docs.rs/memflow/latest/memflow/mem/memory_view/trait.MemoryView.html

## 本地实现确认

- `src/Memory/Memory.cpp`: 直接读和 scatter handle 均使用 no-cache/no-paging 读标志，符合 live 链路现状，但削弱 prefetch 的默认价值。
- `include/Game/Overwatch.hpp`: entity hot fields 已经通过 scatter 读取 velocity/health/hero；字段刷新间隔分层，process 默认 16 ms，hero 50 ms，slow fields 500 ms。
- `include/Game/Decrypt.hpp`: entity header 使用连续结构快照后在本地解析，避免逐字段小读。
- `include/Game/Entity.hpp`: skeleton bone 数据按 block 读取后本地拆解，属于结构/页级合并读取。

## 本轮改动

- 新增 `Memory::PrefetchPages()`，封装 `VMMDLL_MemPrefetchPages` 并纳入 DMA 诊断计数。
- 新增 `Diagnostics::DmaCallsite::EntityPrefetch`，让 prefetch 在 `STATUS`/`SLOW_FRAME` 分解里可见。
- 在 `entity_thread` 中新增环境变量控制的热页 prefetch 探针：
  - `UNLEASHED_DMA_PREFETCH_HOT_PAGES=1`: 启用。
  - `UNLEASHED_DMA_PREFETCH_MAX_PAGES`: 默认 128，范围 8..512。
- 默认不启用 prefetch，现有 scan/process/snapshot 架构保持不变。

## 既有 scatter/合并/缓存优化基线

来自本地既有报告 `docs/dma-read-frequency-optimization-report-20260619-1111.zh-CN.md`：

| 阶段 | process_hz | scan_hz | dma_reads/s | 失败率 | 延迟 avg/max | SLOW_FRAME | 结论 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| scatter 前 | 13.61 | 0.13 | 5944.5 | 2.65% | 512 us / 262103 us | 0 | 高频小读仍较碎 |
| scatter + hot fields 后 | 18.68 | 0.19 | 5968.2 | 3.25% | 491 us / 133796 us | 0 | process_hz 提升，最大延迟下降 |

这说明默认收益应继续来自 scatter 分批读取、结构快照、本地解析和字段分层缓存，而不是额外扩大每帧读面。

## 本轮 live 对比

测试命令：

```powershell
.\build\Release\Unleashed.exe --test-server --test-server-port 19550
```

测试对象：`Connected to Overwatch.exe (cn/ne 150818)`。

| 场景 | 窗口 | process_hz | scan_hz | dma_reads/s | 失败率 | 延迟 avg/max | SLOW_FRAME | 实体发布稳定性 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| prefetch OFF | 12:14:31-12:16:42, 28 samples | avg 17.63, min 13.0, max 22.9 | 0.36 | 6300.4 | 7.96% | 347 us / 159295 us | 0 | `/api/entities` 发布 6 个实体，Alive 6，Visible 6；内部样本 7-8 |
| prefetch ON | 12:17:14-12:18:54, 22 samples | avg 17.33, min 13.9, max 22.1 | 0.35 | 6287.5 | 7.80% | 361 us / 165611 us | 116 | `/api/entities` 发布 6 个实体，Alive 6，Visible 6；内部样本 7 |

prefetch ON 期间日志确认：

```text
[DMA-PREFETCH] hot_pages=103 ok=1 max_pages=128 note=direct_reads_use_nocache.
```

## 判定

- `VMMDLL_MemPrefetchPages`: 已验证可调用，但在当前 no-cache DMA 读路径下没有形成可见收益；默认关闭。
- scatter 分批读取: 继续保留，是当前最可靠的高频小读优化。
- 结构/页级合并读取: 继续保留，entity header 和 skeleton block 的本地拆解方向正确。
- 字段分层缓存: 继续保留，hot/hero/slow/live/dead component 分层能限制 process loop 的随机读面。
- entity_thread: 架构保持 `scan -> process -> snapshot publish`，live 发布稳定；prefetch ON 会增加 SLOW_FRAME 风险。

## 构建与测试

- `.\build-release.ps1`: 成功。
- `ctest --test-dir build -C Release --output-on-failure`: 9/9 通过。

