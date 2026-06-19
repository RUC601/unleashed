# Overlay / Box FPS 优化交接文档

时间：2026-06-19

## 推荐目标模式文本

建议新线程直接使用下面这个 `/goal`：

```text
/goal 基于当前 Unleashed live overlay 环境，先建立正常 overlay 与 box-only 渲染基线，再在保持 scan/process/snapshot 架构、DMA/entity 读取节奏不被盲目拉高的前提下，新增可开关的 box 性能模式、快速 box 绘制路径和渲染侧计时/primitive 统计；小范围优化 PlayerInfo 高频渲染路径，提高 box-only overlay FPS 和正常模式渲染效率；完成 build、config-check、ctest 与 live TestServer/日志对比验证，输出优化前后评估报告。
```

不要把“必须达到 300-400 FPS”写成硬性验收。更好的目标是：把 box 渲染路径的瓶颈量化，做可开关优化，并给出同一 live 环境下的前后对比。如果 live 环境、DWM、显示刷新率或 Present 行为导致达不到 300-400 FPS，也要把瓶颈证据写清楚。

## 给新线程的开场白

可以直接把这段发给新线程：

```text
请先阅读 D:/Desktop/SenseZen/ClaudeCodexCoding/Unleashed/docs/overlay-box-fps-optimization-handoff.zh-CN.md，然后按文档里的目标执行。当前 live 环境我会尽量保持开启。请你自己先建立 baseline，再做小范围代码优化，最后自己 build、config-check、ctest、live TestServer/日志验证，并输出优化前后评估报告。保持 scan/process/snapshot 架构不变，不要为了追 overlay FPS 去把 DMA/entity 读取频率强行拉高。
```

## 项目和工作目录

- 仓库：`D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed`
- 根目录构建脚本：`.\build-release.ps1`
- Canonical build tree：`build`
- Release exe：`build\Release\Unleashed.exe`
- 主要诊断日志：
  - `build\Release\unleashed_diag.log`
  - `build\Release\unleashed_aim_diag.log`
- 现有 DMA 读频优化报告：
  - `docs\dma-read-frequency-optimization-report-20260619-1111.zh-CN.md`

本项目是外部 DX11 + ImGui overlay 应用。不要引入注入、hook、修改目标进程等方向。改动应保持小范围、可编译、可验证。

## 当前背景

上一轮已完成 DMA/entity 高频读取优化，重点是减少 `entity_thread` 高频小随机 DMA 读，并保持 `scan -> process -> snapshot` 架构。结果摘要：

- `process_hz` 平均值：约 `13.61 -> 18.68`
- 每 process cycle 读请求：约 `434.4 -> 332.1`
- `dma_reads/s` 基本持平，因为 process cycle 增多
- 两轮 live 采样均无 `SLOW_FRAME`
- Build、`--config-check`、CTest 已通过

这一轮不要重复做 DMA 读取频率优化。目标转到 overlay render / PlayerInfo / box 绘制路径。

用户的问题是：“还能做什么来提高 overlay 的帧数？尤其是 box 的帧数，我看别人的产品可以三四百帧。”

关键判断：

- “三四百帧”通常指 overlay render loop FPS，不代表 DMA/entity 数据也以 300-400Hz 刷新。
- 本项目应分离 render FPS 和 entity/process FPS：渲染线程可以高频重绘 last snapshot 或插值后的实体；DMA/entity 不应为了 box FPS 被强行拉高。
- 当前 canvas 已经 `Present(0, 0)`，所以不是一个简单的 vsync 开关问题。

## 当前代码热点

### Canvas / Present / frame timing

文件：`src\Renderer\Overlay.cpp`

关注点：

- `MakeSwapChainDesc(...)`
  - 当前 `BufferCount = 2`
  - 当前 `Format = DXGI_FORMAT_R8G8B8A8_UNORM`
  - 当前 `RefreshRate = 60 / 1`
  - 当前 `SwapEffect = DXGI_SWAP_EFFECT_DISCARD`
- `Overlay::RenderCanvas(...)`
  - 每帧执行 ImGui NewFrame
  - 调用 canvas render callback
  - `ImGui_ImplDX11_RenderDrawData(...)`
  - `m_canvasSwapChain->Present(0, 0)`
  - 记录 `FrameTiming { renderCallbackMs, presentMs, totalMs }`
- `Overlay::RenderMenu(...)`
  - menu window 用 `Present(1, 0)`，但 box FPS 主要看 canvas，不要把菜单 Present 当主因。

### 每帧 canvas callback

文件：`src\main.cpp`

函数：`RenderCallback()`

当前每帧大致流程：

```cpp
Diagnostics::RecordFrame();
if connected:
    SnapshotLocalEntity()
    HeroPerkRuntime::UpdateContext(...)
    ProcessHeroSkills()
DrawRadar();
PlayerInfo();
skillinfo();
DrawAimTriggerStatusPanel();
DrawHealthPacks();
DrawFovCircle();
DrawTrackingDeadzoneCircles();
DrawCrosshair();
DrawPipelineDiagnostics();
DrawDiagnosticLogOverlay();
```

这说明“box FPS”并不等于只画 box。正常模式每帧还混着 radar、技能信息、血包、FOV、deadzone、crosshair、诊断面板等。第一阶段应增加 box-only/perf mode，把这些层跳过，先测纯 box 上限。

### PlayerInfo / box 主路径

文件：`include\Game\Overwatch.hpp`

函数：

- `PlayerInfo()`
- `skillinfo()`

`PlayerInfo()` 当前每帧会：

- `SnapshotEntities()`
- `SnapshotLocalEntity()`
- `SnapshotTargetLockRuntime()`
- 取 view matrix snapshot
- 遍历实体并做 render 插值
- 过滤死亡、本地实体、距离、透明度
- `TryBuildProjectedBounds(...)`
- `StabilizeProjectedBounds(...)`
- 计算颜色、outline、可见性、英雄名、图标、血条、文本、骨骼、射线、距离、技能/大招等
- 调 `Render::DrawCorneredBox(...)`

box-only 优化应尽量绕过英雄名、图标、技能、血条、文本、骨骼、射线等非 box 成本。

### Renderer primitives

文件：`src\Renderer\Renderer.cpp`

重点函数：

- `DrawCorneredBox(...)`
- `DrawSeerLikeHealth(...)`
- `DrawInfo(...)`
- `DrawStrokeText(...)`

当前 `DrawCorneredBox(...)` 带 outline 时：

- outline 8 条 `AddLine`
- 主色 8 条 `AddLine`
- 合计每个 box 最多 16 条 line primitive

如果实体数多，outline corner box 会明显增加 ImGui draw list 成本。box perf mode 应增加更便宜的路径，例如：

- `DrawFastRectBox(...)`：1 个 `AddRect`，可选 1 个 `AddRectFilled`
- 或 `DrawCorneredBoxFast(...)`：无 outline，少线条

默认视觉不要改。新快速样式必须可开关，默认关闭或只在 perf mode 开启。

### Diagnostics / TestServer

相关文件：

- `include\Utils\Diagnostics.hpp`
- `src\Utils\Diagnostics.cpp`
- `src\Utils\TestServer.cpp`

当前已有：

- `StatusSnapshot::fps`
- `/api/health` 输出 `diagnostics.fps`
- `STATUS ... fps=...`
- `FrameTiming { totalMs, renderCallbackMs, presentMs }`
- `SLOW_FRAME total/render/present...`

缺口：

- 没有 PlayerInfo 单独耗时
- 没有每帧/每秒 box 数、line 数、rect 数、text 数
- 没有区分正常模式与 box-only 模式的 render workload
- 没有把 primitive/workload stats 暴露到 `/api/health`

## 建议实施顺序

### Phase 0：确认环境和基线材料

先读：

- 本文档
- `docs\dma-read-frequency-optimization-report-20260619-1111.zh-CN.md`
- `src\main.cpp`
- `include\Game\Overwatch.hpp`
- `src\Renderer\Renderer.cpp`
- `src\Renderer\Overlay.cpp`
- `include\Utils\Diagnostics.hpp`
- `src\Utils\Diagnostics.cpp`
- `src\Utils\TestServer.cpp`

确认 worktree 可能已有用户或上一轮改动，不要 reset，不要 revert 未确认的改动。

### Phase 1：建立优化前 live baseline

目标：在不改代码前采样 60-90 秒，至少得到正常模式 baseline。

建议命令：

```powershell
cd D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed
.\build\Release\Unleashed.exe --test-server --test-server-port 19550
```

另开 PowerShell 采样：

```powershell
$base = 'http://127.0.0.1:19550'
1..15 | ForEach-Object {
  $h = Invoke-RestMethod "$base/api/health"
  [pscustomobject]@{
    t = Get-Date -Format 'HH:mm:ss'
    fps = $h.diagnostics.fps
    entities = $h.diagnostics.entity_count
    process_hz = $h.diagnostics.entity_process_hz
    scan_hz = $h.diagnostics.entity_scan_hz
    dma_total = $h.diagnostics.dma_reads.total
    dma_fail = $h.diagnostics.dma_reads.failed
  }
  Start-Sleep -Seconds 5
}
```

同时检查：

```powershell
Invoke-RestMethod http://127.0.0.1:19550/api/entities
Invoke-RestMethod http://127.0.0.1:19550/api/local
Select-String -Path build\Release\unleashed_diag.log -Pattern 'STATUS|SLOW_FRAME|PIPELINE|PlayerInfo'
```

记录：

- 时间窗口
- 样本数
- `fps` avg/min/max
- `entity_count` 范围
- `entity_process_hz` avg/min/max
- `dma_reads/s`、失败率
- `SLOW_FRAME` 数量
- 是否存在异常日志

### Phase 2：先加观测层

建议先做低风险 instrumentation，再做优化。

建议新增或扩展一个 render workload stats：

- 当前模式：normal / box-only
- 每帧 input entities
- projected entities
- drawn boxes
- line primitives
- rect primitives
- filled rect primitives
- text calls
- icon calls
- skeleton line calls
- `PlayerInfoMs`
- `RenderCallbackMs`
- `PresentMs`

实现建议：

- 在 `Diagnostics` 增加轻量 snapshot 结构。
- `Renderer.cpp` 的 draw helper 中计数，或者在 `PlayerInfo()` 侧按调用点计数。
- 每帧结束发布 last-frame stats；每秒可维护 avg/min/max。
- 暴露到 `/api/health` 的 `diagnostics.render` 或同级 `render_workload`。
- 不要在每个 primitive 上写日志，避免日志本身拖慢渲染。

### Phase 3：新增 box-only/perf mode

建议做成可开关，默认关闭。

最小可接受方式：

- CLI flag：`--box-perf-mode`
- 或 config 字段：`boxPerfMode`
- 最好同时把当前 mode 暴露到 `/api/health`

box-only mode 行为：

- 保留 `Diagnostics::RecordFrame()`
- 保留必要的连接状态更新
- 保留 `PlayerInfo()` 或新增 `PlayerInfoPerf()`
- 跳过：
  - `DrawRadar()`
  - `skillinfo()`
  - `DrawAimTriggerStatusPanel()`
  - `DrawHealthPacks()`
  - `DrawFovCircle()`
  - `DrawTrackingDeadzoneCircles()`
  - `DrawCrosshair()`
  - `DrawPipelineDiagnostics()`
  - `DrawDiagnosticLogOverlay()`
- 在 `PlayerInfo()` 中跳过：
  - hero name / battletag / dist text
  - icon lookup and draw
  - healthbar / healthbar2
  - skeleton
  - eye ray / visible eye indicator
  - ultimate / skill cooldown drawing

注意：不要破坏正常模式。默认配置下旧视觉应保持一致。

### Phase 4：新增快速 box 绘制路径

建议新增 Renderer API：

```cpp
void DrawFastRectBox(float x, float y, float w, float h, ImU32 color, float thickness, ImU32 fillColor = 0);
```

实现方向：

- 一次 `ToCanvas` / scale 计算
- `AddRect` 代替 8 或 16 条 `AddLine`
- 可选非常低 alpha 的 fill
- perf mode 默认使用 fast rect
- 正常模式继续用原 `DrawCorneredBox(...)`

如果用户很在意 corner 风格，可以提供 `fastCornerNoOutline`，但第一轮以性能可量化为主。

### Phase 5：小范围 PlayerInfo 热路径优化

优先级从高到低：

1. perf mode 下避免 `GetHeroEngNames(...)`、`FindHeroIcon(...)`、字符串拼接、`CalcTextSize(...)`。
2. 把投影结果收敛成轻量 `ScreenBox` 列表，再绘制。
3. 缓存本帧 `OW::WX/WY`、scale、matrix、target lock 等常量。
4. 避免每个绘制 helper 反复取 `ScaleX()/ScaleY()/ScaleUniform()`，可在 Renderer 层增加 frame scale cache。
5. 只有在前面证明确实卡在 Present/ImGui draw data 后，再考虑 swapchain flip-model / tearing。

不要第一步就改 swapchain。`DXGI_SWAP_EFFECT_FLIP_DISCARD` 和 `DXGI_PRESENT_ALLOW_TEARING` 对透明/layered window 兼容性需要实测，风险比 PlayerInfo/primitive 优化高。

## 验证要求

### 构建和自测

每轮代码改动后至少执行：

```powershell
cd D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed
.\build-release.ps1
.\build\Release\Unleashed.exe --config-check
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build -C Release --output-on-failure
```

如果 build 失败且提示 exe/pdb 被占用，先确认并停止旧进程：

```powershell
Get-Process Unleashed -ErrorAction SilentlyContinue | Stop-Process -Force
```

### Live 对比

至少跑三组：

1. 优化前 normal baseline
2. 优化后 normal mode
3. 优化后 box-only/perf mode

每组建议 60-90 秒，采样间隔 5 秒。尽量保持：

- 同一 live 环境
- 同一 TestServer port
- 实体数量接近
- 不同时打开额外重负载窗口

指标：

- `fps` avg/min/max
- `renderCallbackMs` avg/min/max，如果已暴露
- `presentMs` avg/min/max，如果已暴露
- `PlayerInfoMs` avg/min/max
- `drawn boxes/s` 或 last-frame boxes
- line/rect/text/icon primitive counts
- `entity_process_hz`
- `dma_reads/s` 和 fail rate
- `SLOW_FRAME` 数量
- `/api/entities` 是否仍发布有效实体
- `/api/local` 是否仍发布本地实体

### 视觉/行为验收

正常模式：

- 不应丢失原本启用的 box、血条、名字、技能、radar、FOV 等视觉功能。
- box 位置不应明显漂移。

box-only/perf mode：

- 可以只画 box。
- 需要明确告诉用户这是性能测量/低成本显示模式，不是完整视觉模式。
- box 应仍对应实体位置。

如果 Scenario/OBS 视觉环境可用，优先补一个截图/帧对比；但不要把可见 overlay 当成唯一证明。至少要同时确认 `/api/entities` 与 `/api/local` 正常。

## 输出报告要求

完成后新增报告：

```text
docs\overlay-box-fps-optimization-report-YYYYMMDD-HHMM.zh-CN.md
```

报告建议结构：

- 结论
- 改动列表
- 构建和自测结果
- 优化前 normal baseline
- 优化后 normal mode
- 优化后 box-only/perf mode
- 指标对比表
- 是否接近 300-400 FPS，以及未达到时的瓶颈判断
- 风险和下一步

对比表至少包含：

| 模式 | fps avg/min/max | entities | process_hz | PlayerInfoMs | renderCallbackMs | presentMs | boxes | lines/text/icons | SLOW_FRAME | dma fail rate |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |

## 验收标准

必须满足：

- `.\build-release.ps1` 成功
- `Unleashed.exe --config-check` 成功
- CTest 成功
- live `/api/health`、`/api/entities`、`/api/local` 可用
- 新增模式默认不破坏正常视觉
- 有优化前后同环境对比报告

期望满足：

- box-only/perf mode 的 FPS 有明确提升
- normal mode 的 render workload 或 frame time 不回退
- `entity_process_hz` 不因 render 优化明显下降
- DMA reads/fail rate 不因 render 优化明显恶化

如果 box-only FPS 没有明显提升，不要硬凑结论。应根据 `PlayerInfoMs/renderCallbackMs/presentMs/primitive counts` 判断瓶颈是在：

- PlayerInfo CPU 计算
- ImGui primitive 数量
- DX11 RenderDrawData
- Present/DWM/compositor
- 显示器刷新率/窗口合成
- 其他 live 环境因素

## 明确不要做的事

- 不要改 `scan -> process -> snapshot` 架构。
- 不要为了 300-400 FPS 把 DMA/entity 线程强行拉到 300-400Hz。
- 不要删除或重写现有视觉系统。
- 不要默认改用户当前 box theme。
- 不要把 swapchain flip-model 当第一步大改。
- 不要 reset/revert 当前 worktree 里未确认的改动。
- 不要只看 overlay 画出来就宣称完成；需要日志和 TestServer 证据。

## 当前最可能的高收益点

1. `RenderCallback()` 加 box-only 早退路径。
2. `PlayerInfo()` perf mode 跳过非 box 内容。
3. `DrawCorneredBox()` 增加 fast rect/corner no-outline 路径。
4. Diagnostics 增加 PlayerInfo 和 primitive 统计。
5. 报告中区分 “overlay FPS” 与 “entity/process 刷新率”。

如果这些完成后仍离 300-400 很远，再单独开后续任务评估 flip-model/tearing、custom D3D line renderer、ImGui draw-list 合批或独立 render snapshot cache。
