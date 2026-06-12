# Unleashed Code Reading Notes

Source: D:/Desktop/ClaudeCodexCoding/Unleashed
Snapshot: Git HEAD 10e87de + dirty working tree on 2026-06-10. The dirty files included CMakeLists.txt, include/Game/Overwatch.hpp, include/Game/Structs.hpp, include/Game/WeaponSpec.hpp, include/Utils/Config.hpp, src/Features/UI.cpp, src/Game/WeaponSpec.cpp, src/Utils/Config.cpp, and src/Tools/WeaponSpecDisplaySelfTest.cpp.
Purpose: reading notes only, not runtime source of truth.

EN: This folder is a bilingual reading layer for understanding the current Unleashed codebase without modifying compilable source files.
中文：这个目录是一个中英文双语的源码阅读层，用来理解当前 Unleashed 代码，不修改真正参与编译的源码。

EN: Treat the real source files under `include/` and `src/` as authoritative. Treat these Markdown files as guided notes that may become stale after code changes.
中文：请把 `include/` 和 `src/` 下的真实源码当作权威来源；这里的 Markdown 是阅读笔记，源码变动后可能过期。

## Recommended Order

1. `00-reading-map.zh-CN.md`
2. `01-config-ui-runtime-chain.zh-CN.md`
3. `02-entity-pipeline.zh-CN.md`
4. `03-aim-prediction-fire.zh-CN.md`
5. `annotated-source/include_Game_Overwatch.hpp.zh-CN.md`
6. `annotated-source/src_Game_WeaponSpec.cpp.zh-CN.md`
7. `annotated-source/include_Game_Target.hpp.zh-CN.md`

EN: Read the module notes first, then use the annotated-source files when you want to inspect the important code blocks more slowly.
中文：建议先读模块长文档，再在需要慢慢看关键代码块时进入 `annotated-source`。

## What This Is Not

EN: This is not a source backup, not a generated mirror of the full project, and not something CMake should ever compile.
中文：这不是源码备份，不是整个项目的镜像，也不是 CMake 应该编译的东西。

EN: The files intentionally use `.md` names, including the annotated-source notes, so search results do not look like real `.cpp` or `.hpp` files.
中文：这些文件故意只使用 `.md` 后缀，包括注释版镜像笔记，避免搜索结果看起来像真实 `.cpp` 或 `.hpp` 源码。

## How To Read A Note

EN: Each note uses `EN:` and `中文：` pairs. Code snippets stay close to the real code, while explanations focus on inputs, outputs, state dependencies, and common mistakes.
中文：每篇文档都使用 `EN:` 和 `中文：` 成对说明。代码片段尽量贴近真实源码，解释重点放在输入、输出、状态依赖和常见误解。

EN: When a conclusion says "current snapshot", it means the dirty working tree captured above, not a permanent architectural rule.
中文：如果结论写着“当前快照”，它指的是上面记录的 dirty working tree，不代表永久架构规则。

## Companion Map

EN: The main development guide remains `docs/UNLEASHED_DEVELOPMENT_GUIDE.zh-CN.md`. Use it as the broad map; use this folder as the slow reading notebook.
中文：主开发地图仍然是 `docs/UNLEASHED_DEVELOPMENT_GUIDE.zh-CN.md`。它负责全局导航；本目录负责慢读和逐段理解。
