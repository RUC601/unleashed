# Overlay FPS Task 0-2 Observability and Camera Lane Report

日期：2026-06-25

## 范围

本轮实现 `overlay_fps_dma_kvm_handoff_2026-06-25.md` 中的三个小步：

- Task 0：补齐 render FPS、viewmatrix/entity publish cadence、snapshot copy timing。
- Task 1：保持 `SnapshotEntities()` / `SnapshotDynamicEntities()` 接口，降低 render/TestServer 消费实体快照时和实体线程抢 `g_mutex` 的风险。
- Task 2：补齐 render 消费 viewmatrix 时的 age/stale 统计，用于判断 camera lane 是否限制高帧画面。

未改动：

- offset / profile 值
- 实体解析语义
- aim / trigger 选择逻辑
- render 插值 / 外推策略
- 绘制内容和 ESP 语义

## 新增观测项

`Diagnostics::StatusSnapshot` 现在包含：

- `render_fps`
- `viewmatrix_publish_hz`
- `viewmatrix_publish_age_ms`
- `viewmatrix_publish_last_interval_ms`
- `viewmatrix_publish_max_interval_ms`
- `render_viewmatrix_age_ms`
- `render_viewmatrix_max_age_ms`
- `render_viewmatrix_uses`
- `render_viewmatrix_over_16ms`
- `render_viewmatrix_over_33ms`
- `render_viewmatrix_over_50ms`
- `entity_publish_hz`
- `entity_publish_age_ms`
- `entity_publish_count`
- `entity_publish_last_interval_ms`
- `entity_publish_max_interval_ms`
- `snapshot_entities_copy_ms`
- `snapshot_entities_copy_max_ms`
- `snapshot_dynamic_copy_ms`
- `snapshot_dynamic_copy_max_ms`

这些字段同时暴露在 `/api/diagnostics` 顶层；另有结构化块：

- `diagnostics.publish.viewmatrix`
- `diagnostics.publish.entity`
- `diagnostics.consume.viewmatrix`
- `diagnostics.snapshot_copy.entities`
- `diagnostics.snapshot_copy.dynamic_entities`

## 接入点

- view matrix 成功发布：`TryPublishViewMatrices()` 通过稳定性过滤并写入矩阵后记录 publish cadence。
- entity 发布：`TargetingDetail::SetPublishedEntityCount()` 记录 publish cadence 和最新 count。
- entity snapshot copy：`TargetingDetail::SnapshotEntities()` 记录读取完整快照并复制 vector 的耗时。
- dynamic snapshot copy：`TargetingDetail::SnapshotDynamicEntities()` 记录同类耗时。
- FPS：`Diagnostics::Snapshot()` 现在会更新 FPS，避免 `/api/diagnostics` 读到只由 status dump 刷新的旧值。

## Task 1 快照发布优化

`TargetingDetail::PublishEntitySnapshots()` 现在是唯一实体发布入口：

- 发布者把 `std::vector<c_entity>` / `std::vector<hpanddy>` 在 `g_mutex` 内直接 move 到 `OW::entities` / `OW::hp_dy_entities`。
- `SnapshotEntities()` / `SnapshotDynamicEntities()` 保持原接口，仍复制当前完整快照给调用方，同时记录 copy timing。
- 断开进程时 `ClearProcessRuntimeSnapshots()` 也走同一发布函数，避免旧全局容器和新原子快照清理不同步。

注意：中途尝试过 `std::atomic<std::shared_ptr<const vector<...>>>` 快照，但该版本会在发布热路径里构造新 vector 并同步维护旧全局容器，实际把原本的 move publish 变成了额外 copy / allocation。用户反馈 overlay 从丝滑变成 PPT 后，该方案已回退。当前 Task 1 只保留统一发布入口和观测，不再改变实体热路径的数据结构。

## Task 2 Camera Lane 观测

新增 `Diagnostics::RecordRenderViewMatrixUse()`：

- 在 `PlayerInfoFromSnapshot()` 取 `GetViewMatricesSnapshot()` 后记录一次 render 消费。
- age 来源是最近一次 `RecordViewMatrixPublish()` 的 tick。
- 记录当前 age、累计最大 age、消费次数、缺失 publish 次数，以及 `>16ms` / `>33ms` / `>50ms` 的 stale 次数。

这些指标回答的是：render 每帧绘制时，拿到的矩阵是不是已经老到超过 60Hz / 30Hz 节拍。

## 采样

`scripts/monitor-unleashed-perf.ps1` 已写入新字段到 CSV 和 summary：

- `render_fps`
- `viewmatrix_publish_hz`
- `viewmatrix_publish_age_ms`
- `render_viewmatrix_age_ms`
- `render_viewmatrix_uses_delta`
- `render_viewmatrix_over_16ms_delta`
- `render_viewmatrix_over_33ms_delta`
- `render_viewmatrix_over_50ms_delta`
- `entity_publish_hz`
- `entity_publish_age_ms`
- `snapshot_entities_copy_ms`
- `snapshot_dynamic_copy_ms`

建议启动 runtime 后使用：

```powershell
.\scripts\monitor-unleashed-perf.ps1 -Seconds 60 -IntervalSeconds 1 -BaseUrl http://127.0.0.1:19550
```

## 验证

已通过：

```powershell
.\build-release.ps1
.\build\Release\Unleashed.exe --config-check
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir .\build -C Release --output-on-failure
```

CTest 结果：`10/10 passed`。

`git diff --check` 对本轮改动未报告 whitespace 错误，仅提示 Git 行尾转换。

## Live 采样 1

采样方式：

- 启动 `Unleashed.exe --test-server --test-server-port 19550`
- 采样前临时设置运行目录配置：`aimDryRun=1`、`kmboxEnabled=0`
- 采样后已恢复原配置：`aimDryRun=0`、`kmboxEnabled=1`
- 采样 60 秒，间隔 1 秒

输出文件：

- `logs/perf-monitor-20260625-073432.samples.csv`
- `logs/perf-monitor-20260625-073432.summary.json`

关键结果：

| 指标 | 结果 |
|---|---:|
| sample_count | 60 |
| viewmatrix_publish_hz avg/min/max | 59.315 / 5.813 / 66.666 |
| viewmatrix_publish_age_ms avg/max | 13.817 / 94 |
| entity_publish_hz avg/min/max | 5.746 / 0.877 / 10.752 |
| entity_publish_age_ms avg/max | 144.533 / 515 |
| entity_process_hz avg/min/max | 5.205 / 4.759 / 5.801 |
| snapshot_entities_copy_ms avg/max | 0.004 / 0.015 |
| entity_count avg/min/max | 17.933 / 17 / 19 |
| view_matrix_bad_samples | 0 |
| projection_jump_samples | 0 |
| dma_reads_per_second | 6695.618 |
| dma_fail_rate_percent | 2.537 |

解释：

- 这次安全采样中 `render_fps` 全程为 0，说明该隐藏/安全实例没有捕获到活跃 overlay frame counter；因此本次结果不能作为真实 overlay FPS 结论。
- viewmatrix 发布多数时间接近 60Hz，矩阵状态稳定，没有坏矩阵样本或 projection jump。
- entity 发布平均约 5.7Hz，最大 publish age 到 515ms。当前实体数据节拍明显低于 144/240/300Hz 显示节拍。
- `SnapshotEntities()` 拷贝平均 0.004ms，最大 0.015ms。以这次样本看，render 侧 vector copy / mutex 不是主要耗时。
- `PlayerInfo` / W2S 路径在样本中有正常投影和绘制输入，说明 TestServer 能看到绘制工作量统计。

## Live 采样 2

采样方式：

- Task 1 代码改动后重新构建。
- 启动 `Unleashed.exe --test-server --test-server-port 19551`
- 采样前临时设置运行目录配置：`aimDryRun=1`、`kmboxEnabled=0`
- 采样后已恢复原配置：`aimDryRun=0`、`kmboxEnabled=1`
- 采样 20 秒，间隔 1 秒

输出文件：

- `logs/perf-monitor-20260625-073951.samples.csv`
- `logs/perf-monitor-20260625-073951.summary.json`

关键结果：

| 指标 | 结果 |
|---|---:|
| sample_count | 20 |
| render_fps avg/min/max | 22.566 / 0 / 232.229 |
| render_frame_ms avg/max | 3.310 / 4.938 |
| render_callback_ms avg/max | 0.062 / 0.111 |
| present_ms avg/max | 3.248 / 4.905 |
| viewmatrix_publish_hz avg/min/max | 55.050 / 0 / 66.666 |
| viewmatrix_publish_age_ms avg/max | 16.450 / 78 |
| entity_publish_hz avg/min/max | 21.437 / 0 / 32.258 |
| entity_publish_age_ms avg/max | 96.850 / 1219 |
| snapshot_entities_copy_ms avg/max | 0.002 / 0.012 |
| entity_count avg/min/max | 8.4 / 0 / 12 |
| view_matrix_bad_samples | 2 |
| projection_jump_samples | 11 |
| dma_reads_per_second | 6263.173 |
| dma_fail_rate_percent | 4.424 |

解释：

- 本次隐藏/安全实例采到了短时 `render_fps=232.229`，且 `render_frame_ms` 平均约 3.31ms，说明绘制路径具备 200-300FPS 级预算。
- 平均 FPS 只有 22.566，是因为隐藏/窗口状态下 frame counter 不连续，不能当成最终可见 overlay 的体感 FPS。
- Task 1 后 `SnapshotEntities()` 平均 0.002ms，最大 0.012ms；快照消费仍不是主要耗时。
- `render_callback_ms` 平均 0.062ms，主要时间在 `present_ms`，符合 “render 每帧画最新快照，不等待 DMA 全量读” 的方向。
- startup/隐藏状态下有 2 个 bad viewmatrix 样本和 11 个 projection jump 样本，后续需要在可见 overlay / 真实转视角下复测。

## Live 采样 3

采样方式：

- Task 2 代码改动后重新构建。
- 启动 `Unleashed.exe --test-server --test-server-port 19552`
- 采样前临时设置运行目录配置：`aimDryRun=1`、`kmboxEnabled=0`
- 采样后已恢复原配置：`aimDryRun=0`、`kmboxEnabled=1`
- 采样 20 秒，间隔 1 秒

输出文件：

- `logs/perf-monitor-20260625-075055.samples.csv`
- `logs/perf-monitor-20260625-075055.summary.json`

关键结果：

| 指标 | 结果 |
|---|---:|
| sample_count | 20 |
| render_fps avg/min/max | 20.966 / 0 / 85.132 |
| render_frame_ms avg/max | 3.300 / 9.321 |
| render_callback_ms avg/max | 0.070 / 0.103 |
| present_ms avg/max | 3.230 / 9.258 |
| viewmatrix_publish_hz avg/min/max | 54.425 / 0 / 66.666 |
| viewmatrix_publish_age_ms avg/max | 13.200 / 31 |
| render_viewmatrix_age_ms avg/min/max | 7.000 / 0 / 31 |
| render_viewmatrix_uses_delta | 927 |
| render_viewmatrix_over_16ms_delta | 55 |
| render_viewmatrix_over_33ms_delta | 38 |
| render_viewmatrix_over_50ms_delta | 34 |
| render_viewmatrix_missing_publish_delta | 0 |
| entity_publish_hz avg/min/max | 8.740 / 0 / 62.500 |
| entity_publish_age_ms avg/max | 166.400 / 1219 |
| snapshot_entities_copy_ms avg/max | 0.002 / 0.005 |
| view_matrix_bad_samples | 2 |
| projection_jump_samples | 3 |
| dma_reads_per_second | 6243.254 |

解释：

- render 当前消费的 viewmatrix age 平均约 7ms，采样点最大 31ms；普通状态下并不是每帧都很旧。
- 927 次 render 消费里，55 次超过 16ms，38 次超过 33ms，34 次超过 50ms。这个比例不高，但已经能说明 camera lane 有偶发 stale 窗口。
- `render_viewmatrix_max_age_ms` 最大 188ms 是累计最大值，包含启动阶段，不等同于本采样窗口的当前 age 最大值。
- render callback 仍然只有约 0.07ms，snapshot copy 约 0.002ms；这次数据继续支持“绘制和快照复制不是主瓶颈”。

## PPT 回归修复

用户反馈 overlay 体感从丝滑回退到 PPT 后，检查到两个高风险点：

- Task 1 初版把实体发布从 move 改成了发布期 copy / allocation，再同步旧容器，方向错误，已回退为 `PublishEntitySnapshots(std::vector&&, std::vector&&)` 内部 move。
- `hero_change` 在实体线程里使用 `Diagnostics::Info` 无条件打印；现场 log 里该项大量刷屏。现在只在 `OW::PipelineDebugEnabled()` 为 true 时打印，避免正常运行时日志 I/O 抢实体线程。

回归修复后重新验证：

```powershell
.\build-release.ps1
.\build\Release\Unleashed.exe --config-check
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir .\build -C Release --output-on-failure
```

CTest 结果：`10/10 passed`。

安全烟测：

- 临时配置：`aimDryRun=1`、`kmboxEnabled=0`
- 启动：`Unleashed.exe --test-server --test-server-port 19553`
- 输出：
  - `logs/perf-monitor-20260625-080759.samples.csv`
  - `logs/perf-monitor-20260625-080759.summary.json`
- 采样后已恢复：`aimDryRun=0`、`kmboxEnabled=1`

关键结果：

| 指标 | 结果 |
|---|---:|
| sample_count | 15 |
| render_fps avg/min/max | 13.026 / 0 / 69.374 |
| render_frame_ms avg/max | 2.901 / 6.281 |
| render_callback_ms avg/max | 0.027 / 0.033 |
| present_ms avg/max | 2.874 / 6.253 |
| viewmatrix_publish_hz avg/min/max | 53.194 / 0 / 66.666 |
| entity_publish_hz avg/min/max | 33.267 / 0 / 66.666 |
| snapshot_entities_copy_ms avg/max | 0.000 / 0.001 |
| slow_frame_effective_count | 0 |

这次烟测里 `entity_count=0`，所以它只能证明修复后隐藏/安全实例没有 slow frame，不能替代可见 overlay 的真实体感复测。

## 二次现场检查

用户重启新版后反馈仍然 PPT。检查顺序：

- 重新阅读交接文档：原始建议的 Task 0 是只补观测，不改结构；当前工作树已经包含 Task 0/1/2 和一次 Task 1 回退。
- 当前运行进程：`Unleashed.exe` PID `29136`，启动时间 `2026-06-25 08:12:00`，未开启 test server 监听。
- 当前配置：`aimDryRun=0`、`kmboxEnabled=1`、`kmboxDebugLog=0`。
- 当前 `STATUS-CADENCE`：`snapshot_copy_ms` 最大约 `0.026ms`，没有 `SLOW-FRAME` 记录；这不支持“实体 snapshot copy 是 PPT 主因”。

现场主日志统计：

| 指标 | 数量 |
|---|---:|
| total log lines | 6919 |
| `[KMBOX-NET]` lines | 3342 |
| `output send` lines | 3168 |
| `input button state changed` lines | 168 |
| `hero_change` lines | 63 |
| `STATUS-CADENCE` lines | 63 |
| `SLOW-FRAME` lines | 0 |

结论：

- 当前修改里，Task 1 shared snapshot 的方向确实有问题，已经回退。
- 回退后仍 PPT 的更强现场证据，是 `kmboxDebugLog=0` 下 KMBox 正常输出仍持续 `Diagnostics::Info` 刷主日志。
- 这类 mouse_move 级别的磁盘 I/O 不应该出现在正常运行路径；它会抢 CPU/锁/磁盘写入，足以让 overlay 体感像 PPT。

已追加修复：

- `src/Kmbox/KmBoxNetManager.cpp`：`input button state changed`、`keyboard report changed`、`output send count` 只在 `OW::Config::kmboxDebugLog` 为 true 时写主日志。
- `src/Kmbox/KmboxB.cpp`：串口 KMBox 的 `output send count`、`km_left/km_right queue output` 也只在 debug 开启时写主日志。
- 发送逻辑、队列逻辑、KMBox 输出行为未改；只关闭正常模式下的高频 Info 落盘。

重新验证：

```powershell
.\build-release.ps1
.\build\Release\Unleashed.exe --config-check
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir .\build -C Release --output-on-failure
```

结果：build 通过，config-check 通过，CTest `10/10 passed`。

## 三次现场检查

用户重启 `2026-06-25 08:17:33` 构建后反馈仍然 PPT。再次检查当前运行态：

- 当前进程确认为新 exe：`D:\Desktop\SenseZen\ECS_O\01_PRODUCTS\un-dma\build\Release\Unleashed.exe`。
- `kmboxDebugLog=0` 下，主日志里 `[KMBOX-NET]` 只剩 6 行，`output send=0`；KMBox 主日志刷屏已排除。
- render FPS 在可见阶段能到约 `407-415FPS`，说明前端绘制循环本身没有卡死。
- `SLOW_FRAME` 主要是 `present=16-23ms`，`render=0.0-0.1ms`；这像 Present/DWM 阶段偶发等待，不是 ESP 绘制 CPU 耗时。
- `entity_pub_hz` 会掉到 `1.6-3.8Hz`，`entity_publish_age_ms` 到 `218-296ms`；这会让实体框/骨骼在多帧内持同一份旧坐标。
- `viewmatrix_pub_hz` 多数约 `62.5-66.7Hz`，有一次掉到 `32.3Hz`、age `62ms`；camera lane 仍有偶发 stale，但不是每秒都坏。
- `snapshot_copy_ms` 最大约 `0.099ms`；snapshot copy 仍不是主要瓶颈。

关键代码判断：

- 现有 `InterpolateEntityForRender()` 只做 previous/current 之间的 16ms 延迟插值。
- 一旦 render 时间追上当前实体快照，它就直接返回当前坐标；当实体 publish 只有 3-20Hz 时，400FPS render 会连续几十到上百帧画同一个旧位置，体感就是 PPT。

已追加修复：

- `include/Game/Overwatch.hpp` 新增 `ApplyShortRenderExtrapolation()`。
- 只在绘制阶段启用，不改 DMA 读、不改实体发布、不改 aim 选择坐标。
- 触发条件：实体有上一帧样本、当前 render 已追上最新实体样本、样本 age 在 `12-160ms` 内、速度可信。
- 限制：最大 lead `100ms`，最大位移 `1.35m`，并同步平移 box/skeleton/head/chest 等绘制点。

重新验证：

```powershell
.\build-release.ps1
.\build\Release\Unleashed.exe --config-check
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir .\build -C Release --output-on-failure
```

结果：build 通过，config-check 通过，CTest `10/10 passed`。

## 四次现场检查

用户反馈另一个线程改了 overlay 按距离淡出后又变成 PPT。检查当前工作树和运行态后，结论如下：

- 当前源码里的 `DistanceOpacity(float distance)` 已经固定返回 `1.0f`，没有启用按距离淡出；`visualMaxDist` 只作为距离裁剪 gate。
- 运行日志在 `2026-06-25 08:39` 后出现 `render_fps=0.0`，但 `viewmatrix_pub_hz` 和 `entity_pub_hz` 仍在更新；这说明 DMA/backend 没停，问题在 canvas 渲染回调没有持续执行。
- 根因是 overlay 窗口状态机：菜单失焦会调用 `MinimizeToTaskbar()`，它会隐藏 canvas 并设置 `m_minimizedToTaskbar=true`；随后 `Overlay::Run()` 每轮 `Sleep(16)` 后跳过 `RenderCanvas()`。OBS/画面会停留在最后一帧，体感就是 PPT。
- 另外，`Diagnostics::UpdateFps()` 原来只要 snapshot 间隔超过 `1ms` 就刷新 FPS；菜单 UI 也会读取 `Snapshot()`，导致状态日志可能在两帧之间把 `render_fps` 刷成 `0.0`，造成误判。

已追加修复：

- `src/Renderer/Overlay.cpp`：菜单失焦只隐藏设置菜单，不再把 canvas 进入 taskbar-minimized/pause 状态。
- `src/Renderer/Overlay.cpp`：只有明确收到 `SC_MINIMIZE` 时才调用 `MinimizeToTaskbar()`，保留用户主动最小化时暂停 overlay 的行为。
- `include/Renderer/Overlay.hpp`：将 `m_canMinimizeOnMenuDeactivate` 改为 `m_canHideMenuOnDeactivate`，避免语义继续误导。
- `src/Utils/Diagnostics.cpp`：FPS 采样窗口改为至少 `250ms`，防止多个 snapshot consumer 把 FPS 统计抖成 0。

现场验证：

- 新进程：PID `31544`，`Unleashed Settings` 正常启动。
- 隐藏菜单后，`STATUS-CADENCE` 连续显示 `render_fps=391.7`、`render_fps=412.9`。
- 同期 `snapshot_copy_ms` 最大约 `0.004ms`，`entity_pub_hz` 约 `21.3Hz`，canvas 未再冻结。

重新验证：

```powershell
.\build-release.ps1
.\build\Release\Unleashed.exe --config-check
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir .\build -C Release --output-on-failure
git diff --check
```

结果：build 通过，config-check 通过，CTest `10/10 passed`，`git diff --check` 仅有既有 LF/CRLF 提醒。

## 下一步判断

- viewmatrix publish age 是否在快速转视角时上升
- entity publish Hz 是否低于预期
- snapshot copy ms 是否在实体多或 TestServer 查询时 spike
- render callback / present 是否仍是主要耗时

基于三次样本，render 绘制本身已经显示出 200FPS+ 峰值和 3-5ms frame time，快照 copy 也在微秒级。下一步不应继续围绕 vector copy 大改；更有价值的是继续 Task 2 的小范围 camera lane 实验，或进入 Task 3 的实体 age/interpolation：

- 在可见 overlay / 真实快速转视角下复测 render FPS、present、viewmatrix age。
- 评估把 viewmatrix 成功路径 `Sleep(5)` 降到更小值或做可配置高频模式时，是否能减少 `render_viewmatrix_over_16/33/50ms`，以及 DMA reads/sec 是否明显上升。
- 记录每个实体 snapshot age，再决定只插值还是短窗口外推。
