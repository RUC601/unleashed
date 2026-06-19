# Overlay / Box FPS 优化报告

时间：2026-06-19 11:47

## 结论

本轮完成了可开关的 `boxPerfMode`、快速 rect box 绘制路径、渲染侧 timing / primitive 统计，以及 `PlayerInfo()` 高频路径的 box-only 早退优化。

关键结果：

- 优化前 normal baseline：平均 `23.50 FPS`，实体数稳定 `7`。
- 优化后 normal mode：平均 `24.04 FPS`，默认视觉路径未改，primitive 和 timing 可观测。
- 最终 box-only/perf mode：平均 `59.94 FPS`，`SLOW_FRAME=0`，每帧 box 绘制从 corner/outline 多 line 降为 fast rect，text/icon/line 归零。
- box-only 没有把 DMA/entity 线程强行拉高；`process_hz` 在最终 perf 采样中约 `16.79 Hz`，仍是 scan/process/snapshot 架构下的实体刷新。
- 没有接近 300-400 FPS。最终瓶颈已经不是 `PlayerInfo()` 或 box primitive，而是 60Hz present/compositor/frame pacing：`render_callback_ms` 约 `0.026ms`，`present_ms` 约 `7.64ms`，`fps` 稳定贴近 60。

## 改动列表

- `include/Utils/Config.hpp`、`src/Utils/Config.cpp`
  - 新增 `boxPerfMode`，默认 `false`。
  - 新增 `boxPerfFastRect`，默认 `true`。
  - 接入 config load/save/config dump。
- `src/main.cpp`
  - 新增 CLI：`--box-perf-mode`。
  - normal mode 保持原 render callback 行为。
  - box perf mode 跳过 radar、skillinfo、health pack、FOV、deadzone、crosshair、pipeline/log overlays。
  - box perf mode 下跳过 `ProcessHeroSkills()`，避免 render thread 每帧继续跑非 box 所需的技能处理和 DMA 读。
- `include/Game/Overwatch.hpp`
  - `PlayerInfo(bool boxPerfMode = false)` 增加 box-only 快速路径。
  - perf mode 仅保留 snapshot、local snapshot、target lock、view matrix、投影/稳定和 box 绘制。
  - perf mode 跳过 hero name、battletag、healthbar、icon、skeleton、eye ray、distance、ultimate、skill cooldown 等非 box 内容。
- `include/Renderer/Renderer.hpp`、`src/Renderer/Renderer.cpp`
  - 新增 `DrawFastRectBox(...)`。
  - 为 line/rect/filled/text/icon/corner box/fast box 增加轻量计数。
- `include/Utils/Diagnostics.hpp`、`src/Utils/Diagnostics.cpp`、`src/Utils/TestServer.cpp`
  - 新增 render workload snapshot。
  - `/api/diagnostics` 暴露 `diagnostics.render.mode`、`frame_ms`、`render_callback_ms`、`present_ms`、primitive counts。
  - `/api/diagnostics.player_info` 暴露 `elapsed_ms`、`box_perf_mode`、`fast_box_path`。

## 构建和自测

全部通过：

- `.\build-release.ps1`
- `.\build\Release\Unleashed.exe --config-check`
- `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build -C Release --output-on-failure`

CTest 结果：`9/9 passed`。

## Live 采样

采样端点：`http://127.0.0.1:19550/api/diagnostics`

每组正式采样：15 个样本，间隔 5 秒，实体数稳定为 7。`/api/health`、`/api/entities`、`/api/local` 在最终 perf mode 验证中均可用。

| 模式 | FPS avg/min/max | entities | process_hz avg/min/max | PlayerInfoMs avg | renderCallbackMs avg | presentMs avg | boxes avg | primitives avg | SLOW_FRAME | DMA reads/s | DMA fail rate |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 优化前 normal baseline | 23.50 / 20.20 / 26.40 | 7-7 | 7.37 / 5.66 / 8.97 | N/A | N/A | N/A | N/A | N/A | N/A | 6367.23 | 7.75% |
| 优化后 normal | 24.04 / 21.80 / 26.80 | 7-7 | 9.17 / 6.71 / 12.72 | 0.0196 | 36.67 | 6.20 | 4.80 | line 57.6 / rect 22.0 / fill 44.0 / text 25.8 / icon 7.0 | N/A | 6172.93 | 4.98% |
| 最终 box-only/perf | 59.94 / 59.79 / 60.00 | 7-7 | 16.79 / 14.49 / 20.00 | 0.0063 | 0.026 | 7.64 | 4.60 | line 0 / rect 4.67 / fill 0 / text 0 / icon 0 | 0 | 6049.10 | 2.88% |

说明：

- pre-baseline 是加 instrumentation 前采样，因此没有 `PlayerInfoMs/renderCallbackMs/presentMs/primitive` 字段。
- normal post 采样后又启动了 perf 进程，根目录 `unleashed_diag.log` 被重新初始化；normal 的 SLOW_FRAME 日志计数不作为可比数据。
- 最终 box-only/perf 窗口 `2026-06-19 11:45-11:46` 日志统计：`STATUS=16`，`SLOW_FRAME=0`。

## 中间发现

第一版 box perf mode 只跳过了视觉层并改为 fast rect，但仍在 render callback 顶部每帧调用 `ProcessHeroSkills()`。该版本表现为：

- `fps_avg=24.00`
- `PlayerInfoMs=0.0089`
- `renderCallbackMs=34.61`
- primitive 已降到 fast rect，但 render thread 日志仍显示每帧存在 DMA 读。

因此真正的 FPS 提升来自第二步：box perf mode 下跳过 `ProcessHeroSkills()`，让 box-only 渲染只消费已发布 snapshot，而不是在 render thread 继续做非 box 的技能读取。

## 瓶颈判断

最终 perf mode 下：

- `PlayerInfoMs` 已降到约 `0.006ms`。
- `renderCallbackMs` 已降到约 `0.026ms`。
- 每帧 primitive 基本只剩 `rect ~= boxes`。
- FPS 稳定贴近 `60`，没有继续向 300-400 上升。

所以当前 300-400 FPS 的阻碍不是 box 计算和 ImGui primitive 数量，而是现有 DX11 transparent overlay + DWM/compositor/present pacing 的 60Hz 行为。若后续要继续冲高 FPS，应单独评估 flip-model/tearing、独立 D3D box renderer、或可验证的 present pacing 改造；这些风险高于本轮小范围优化。

## 风险和下一步

- `boxPerfMode` 是性能测量/低成本显示模式，不是完整视觉模式；它会跳过技能/血条/图标/文本/radar/FOV 等内容。
- normal mode 默认不开启 `boxPerfMode`，原视觉路径不变。
- 如果要把 box-only 模式做成 UI 开关，可以在 Visual/Misc 面板补一个 checkbox；当前已有 config/CLI 开关。
- 若继续追 300-400 FPS，建议先做 present/compositor 专项实验，不要再提高 DMA/entity 读取频率。
