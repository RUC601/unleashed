# un-dma Agent Startup Notes

本仓库 `D:\Desktop\SenseZen\ECS_O\01_PRODUCTS\un-dma` 是旧
`D:\Desktop\SenseZen\ClaudeCodexCoding\Unleashed` 的当前主 DMA runtime
checkout。未来涉及 Unleashed / un-dma / 旧 ClaudeCodexCoding 路径的工作，
优先以本仓库为当前工作目录。

## 项目边界

- 本仓库只保留 runtime 必需逻辑：overlay、config、KMBox、TestServer、DMA runtime、runtime profile 接入。
- offset 静态分析、探针、现场读数和验证工具不放回本仓库；默认走
  `D:\Desktop\SenseZen\ECS_O\03_DOWNP\vertifytool`。
- 旧 `ClaudeCodexCoding\Unleashed` 路径只作为历史记忆索引，不作为当前 checkout。

## 默认验证

常规代码改动后优先运行：

```powershell
.\build-release.ps1
.\build\Release\Unleashed.exe --config-check
```

涉及 self-test、配置迁移、预测、hero skill、KMBox mock、TestServer 或共享行为时，再运行：

```powershell
ctest --test-dir .\build -C Release --output-on-failure
```

如果 shell 找不到 `ctest`，优先使用 Visual Studio bundled CTest，或先确认
`.\build-release.ps1` 已完成常规构建。

## Offset 工作规则

offset / profile 更新不能直接复制候选值进 runtime。默认顺序是：

1. 静态/source 证据。
2. read-only live probe。
3. 小范围 runtime 接入。
4. build / config-check / live JSON / 视觉验证。

Scenario、OBS、overlay 和 TestServer JSON 是最终现象验证，不替代静态结构证明。

## 注意事项

- 不要把旧路径里的文件状态当成当前状态；当前状态以本仓库为准。
- 不要把 `Offsets.hpp` 的旧注释值、历史候选、diagnostic-only 值当成 runtime truth。
- 不要把临时 dump、probe、scanner、研究脚本随手塞回主 runtime 仓库。
- 触碰配置语义时，必须同时考虑配置加载、迁移、自检和 `--config-check` 输出。
- 触碰 KMBox / TestServer / profile 相关路径时，保留低风险、可回退、可验证的改动边界。
