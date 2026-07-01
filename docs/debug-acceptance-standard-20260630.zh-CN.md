# un-dma 调试验收标准

本文定义 `D:\Desktop\SenseZen\ECS_O\01_PRODUCTS\un-dma` 的调试目标和验收边界。它不是功能设计文档，而是每次修 bug、调性能、接 profile、查 overlay 现象时用来判断“是否可以收工”的标准。

核心原则：

```text
编译通过只说明代码能产物化，不说明现象已修好。
JSON 指标能证明运行状态，不替代 overlay / OBS 视觉验收。
视觉感觉变好但缺少静态或 telemetry 证据，不算可复现修复。
每轮只围绕一个假设改动，验收失败就继续定位或回滚。
```

## 0. 验收前提

开始改代码前，必须先写清楚本轮的验收目标：

```text
问题现象：
复现场景：
当前目标进程 / profile / config 路径：
本轮假设：
本轮只改什么：
本轮不碰什么：
通过标准：
失败标准：
证据保存位置：
```

如果这些内容说不清，本轮还处在观察阶段，不应把 patch 当成修复结论。

## 1. 通用底线

所有 runtime 代码改动至少要满足：

```powershell
.\build-release.ps1
.\build\Release\Unleashed.exe --config-check
```

涉及 self-test、配置迁移、预测、hero skill、KMBox mock、TestServer 或共享行为时，再跑：

```powershell
ctest --test-dir .\build -C Release --output-on-failure
```

如果当前 shell 找不到 `ctest`，本机优先使用：

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir .\build -C Release --output-on-failure
```

收工前额外检查：

```powershell
git diff --check
git status --short
```

`git status` 可以是脏的，因为仓库里可能有用户或实验改动；但本轮结论必须说清楚自己新增或修改了哪些文件，不把既有脏状态算作本轮成果。

## 2. Live 环境有效性

live 验收必须先证明环境真的有效。启动 runtime：

```powershell
.\build\Release\Unleashed.exe --test-server --test-server-port 19550
```

基础 API：

```powershell
Invoke-RestMethod http://127.0.0.1:19550/api/health
Invoke-RestMethod http://127.0.0.1:19550/api/local
Invoke-RestMethod "http://127.0.0.1:19550/api/entities?team=enemy&include_dead=0"
Invoke-RestMethod http://127.0.0.1:19550/api/diagnostics
```

有效 live 的最低要求：

```text
/api/health 能返回 JSON。
process.connected=true，或结论明确写为“未连接目标，只完成 build/config smoke”。
/api/local 与当前本地英雄、alive、health、坐标状态不明显矛盾。
/api/entities 在有敌方实体的场景下能发布合理实体，不用历史缓存充数。
/api/diagnostics 的关键计数在采样窗口内继续变化，而不是停在旧值。
```

如果目标进程没开、TestServer 没启动、FPGA/KMBox/DMA 设备没连上，不能验收 runtime 现象，只能验收静态和离线 smoke。

## 3. 性能和稳定性采样

性能类、PPT 类、实体刷新类、scan/cache 类调试，默认用 60 到 120 秒采样。短 smoke 可以 15 到 30 秒，但不能作为最终验收。

采样命令：

```powershell
.\scripts\monitor-unleashed-perf.ps1 -Seconds 120 -IntervalSeconds 1 -BaseUrl http://127.0.0.1:19550 -Label "<label>"
```

对比命令：

```powershell
.\scripts\compare-unleashed-perf.ps1 -Baseline .\logs\<baseline>.summary.json -Candidate .\logs\<candidate>.summary.json
```

尖峰分析：

```powershell
.\scripts\analyze-unleashed-spikes.ps1 -Summary .\logs\<candidate>.summary.json
```

性能 patch 的通过标准：

```text
有 baseline summary 和 candidate summary。
candidate 的目标指标按预先声明方向改善。
非目标关键指标不能明显回退。
采样期间无崩溃、无 TestServer 断连、无 overlay 卡死。
如果 compare verdict 是 candidate-regressed，本轮不能通过。
```

常看指标：

```text
render_fps
entity_publish_hz
entity_publish_age_ms
viewmatrix_publish_age_ms
scan_get_ow_entities_ms
entity_cycle_ms
projection_jump_samples
scan_publish_attempt_delta
scan_topology_rescan_request_delta
sdk_begin_frame_scan_delta
dma_window_*_max_us
```

默认警戒线：

```text
entity_publish_age_ms 或 viewmatrix_publish_age_ms 稳态频繁超过 1000ms：失败。
render_fps 长时间为 0，且不是菜单/隐藏/安全实例的已解释状态：失败。
projection_jump_samples 相比 baseline 明显增加：失败。
目标 scan/cache 优化让 entity freshness 明显变差：失败。
只有平均值改善但 max spike 明显变差：继续观察，不能直接通过。
```

## 4. Overlay / OBS 视觉验收

Overlay、投影、框漂移、全屏跳变、PPT、render prediction 相关调试必须有视觉证据。TestServer JSON 只能说明内部状态，不能替代画面。

最低视觉验收：

```text
OBS 或 Scenario 捕获到真实 host 画面。
画面里有可观察对象，不能只看静止菜单或空场景。
overlay 框、骨骼、目标点与画面对象同步移动。
快速转视角、目标移动、短暂停顿后不出现整屏漂移或全局跳变。
至少连续观察 60 秒；PPT/卡死类问题建议 120 秒。
```

Scenario / OBS 端口约定：

```text
9808 = host screen stream
9809 = local / secondary-machine stream
```

视觉通过必须同时记录：

```text
捕获时间：
捕获命令或工具：
截图 / 视频 / frame 路径：
肉眼观察结论：
对应 TestServer summary 路径：
```

如果视觉仍然错位、闪烁、PPT，即使 build 和 metrics 看起来好，本轮也不通过。

## 5. Offset / Profile 类验收

offset / runtime profile 更新不能直接复制候选值进 runtime。通过顺序必须是：

```text
1. 静态/source 证据：结构、xref、调用链或字段语义成立。
2. read-only live probe：在 `03_DOWNP\vertifytool` 侧验证候选值能读到合理现场数据。
3. 小范围 runtime 接入：只改必要的 profile/offset/runtime glue。
4. build / config-check / live JSON / 视觉验证。
```

失败标准：

```text
只因为 `Offsets.hpp` 旧注释或历史候选看起来像，就改 runtime：失败。
只有 Scenario/OBS 现象，没有静态结构证明：失败。
只在 probe 里能读，runtime `/api/local` 或 `/api/entities` 不合理：失败。
引入临时 dump、scanner、研究脚本到主 runtime 仓库：失败。
```

## 6. Entity / ViewMatrix / Projection 类验收

这类问题不能只看一个层面的数字。必须同时看：

```text
实体发布：entity_publish_hz / entity_publish_age_ms / entity_count
矩阵发布：viewmatrix_publish_age_ms / render_viewmatrix_age_ms
投影稳定：projection_jump_samples / projection_stability
视觉现象：框和目标是否贴合真实画面
```

通过标准：

```text
稳定场景内实体和矩阵 age 不持续积压。
实体 count 与实际场景大致一致。
projection_jump_samples 不比 baseline 明显变差。
画面里没有整屏跳、框集体漂移、长时间停帧。
```

如果 FPS 上去了，但 entity freshness 变差，不能把它算作完整修复。最多算“render 第一阶段改善”。

## 7. KMBox / 输入类验收

KMBox、鼠标、热键、点击相关问题要先区分 runtime 代码和硬件/透传状态。

最低验收：

```text
确认目标进程和 runtime 是否在跑。
确认 KMBox 连接、设备 passthrough、网络/串口状态。
确认输入事件在正确层出现。
修复尽量落在具体 hotkey / activation 路径，不在底层 reader 做大范围修正。
```

通过标准：

```text
目标操作在 live 场景中连续可复现。
没有引入额外误触发、长按卡住、右键/侧键互相影响。
如果硬件状态不满足，只能结论为 hardware/passthrough blocker，不能改代码硬凑。
```

## 8. 通过 / 继续 / 回滚判定

本轮结论只允许三类：

```text
通过：
  静态/build/live/视觉证据都满足本轮目标，无明显回退。

继续：
  有改善但证据不完整，或发现了更小的下一步假设。

回滚：
  目标指标回退、视觉仍失败、引入 crash/config/self-test 失败，或 patch 变成大范围不确定改动。
```

回滚不是失败，是调试闭环的一部分。特别是性能实验，candidate-regressed 就应尽快回到 baseline，再换一个更窄假设。

## 9. 每轮交付模板

每次调试结束，按这个格式留下结论：

```text
目标：
改动：
未改：
验证命令：
live 环境：
baseline 证据：
candidate 证据：
视觉证据：
结论：通过 / 继续 / 回滚
下一步：
```

示例：

```text
目标：降低 steady-state scan 拖慢导致的 entity age spike。
改动：只调整 scanner cache 的 feature-gated 路径。
未改：offset、KMBox、UI、目标选择。
验证命令：build-release、config-check、CTest、120s monitor、compare、spike analysis。
live 环境：Overwatch.exe connected=true，TestServer 19550。
baseline 证据：logs/perf-monitor-baseline-....summary.json
candidate 证据：logs/perf-monitor-candidate-....summary.json
视觉证据：Scenario capture frame/video 路径。
结论：继续，scan avg 降低但 entity_publish_age_ms max 变差，需要下一轮定位 spike。
下一步：只看 spike 样本对应 top_process_phase 和 dma_window callsite。
```

## 10. 一句话目标

以后 un-dma 调试的目标不是“看起来可能修了”，而是：

```text
一个清楚假设，一个窄 patch，一组可复跑命令，一份 live summary，一份视觉证据，一个明确 verdict。
```
