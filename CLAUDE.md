# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

3D game engine and visual editor built with C++20 / OpenGL 4.3, created as a graduation project. The codebase is in English but comments are often in Chinese.

## Build Commands

```bash
# First-time setup: initialize git submodules (vendors are submodules)
git submodule update --init --recursive
```

### Linux / VM (Ninja + vcpkg)

```bash
# 需先设置 VCPKG_ROOT；首次配置会按 vcpkg.json 安装依赖
cmake --preset linux-default
cmake --build build --target Editor
# Run
./build/Editor/Editor
```

### Windows

Windows 构建命令见 `CLAUDE.local.md`（含 VS Build Tools 完整路径）。

> **注意：** Linux/Ninja 输出路径无配置子目录，Windows/VS 生成器输出路径含 `RelWithDebInfo/` 子目录。
> 两个 preset 的输出目录相同（`build/`），切换 preset 后需重新配置但构建/运行路径不变。

**Build targets:** `Engine` (static lib), `Editor` (exe), `Sandbox` (exe), `Server` (headless exe), `EngineTests` (exe，`BUILD_TESTING=ON` 时可用)。

**Run:** 编辑器与服务器启动时会自动检测项目根目录，因此可从任意目录启动。

### Tests

```bash
# Linux：配置后构建并运行单元测试
cmake --build build --target EngineTests
ctest --test-dir build --output-on-failure

# Windows：构建命令使用 CLAUDE.local.md 中的 CMake 完整路径
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target EngineTests
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

网络同步的协议冒烟与多终端演示说明见 `docs/server/testing.md`；测试场景和服务器应先启动，不要默认网络测试可以直接无人值守执行。

## Architecture

| 目录 | 职责 |
|------|------|
| `Editor/` | 可视化编辑器（exe），分层协调式架构（EditorLayer → 控制器/面板）；支持局域网网络客户端 |
| `Engine/` | 引擎静态库（Core, Renderer, Scene, Reflection, Asset, Physics, Audio, Media, Terrain, Script, Network, Events, Debug, ImGui） |
| `Engine/Platform/OpenGL/` | OpenGL 4.3 具体实现（默认后端） |
| `Engine/Platform/Vulkan/` | Vulkan 1.2+ 具体实现（可选，`--vulkan` 命令行启用） |
| `Engine/Platform/CUDA/` | CUDA compute sidecar（粒子/SPH，需 `ENGINE_ENABLE_CUDA=ON`），含错误检测 + 全局中毒回退 |
| `Server/` | 服务器权威的无头联机服务进程，复用 Engine 的场景、物理与网络协议 |
| `Sandbox/` | 独立示例程序（exe） |
| `tests/` | GoogleTest 单元测试（`EngineTests`）及 `manual/` 网络冒烟/演示脚本 |
| `vendor/` | 第三方库（glfw, glm, spdlog, entt, yaml-cpp, imguizmo, stb_image, imgui, assimp, bullet3） |
| `assets/` | GLSL 着色器、模型、纹理、场景（`.scene` = YAML） |
| `remotion/` | 独立的 React / Remotion / Three.js 视频工程，用于 SPH 原理与数据可视化；用法见 `remotion/README.md` |
| `docs/` | 技术调研报告、竞赛文档、论文、性能分析、Vulkan 迁移与网络同步设计等 |

各模块的详细约定见 `.claude/rules/` 下的路径限定规则文件。

## Language

- 所有回答、解释、注释均使用**简体中文**。

## Working Style

- **遇到不确定的信息时必须主动向用户提问，禁止猜测或脑补。** 宁可多问一句，也不要基于假设给出错误结论。
- 这包括但不限于：运行场景参数（粒子数量、迭代次数等）、用户意图、项目背景、复现步骤等。
- **修复 bug 时，必须实施实际的代码修复。** 不要止步于产出计划、验证文档或分析报告。完整流程：验证 bug → 实施修复 → 构建验证 → 提交。
- **当用户请求建议或意见**（演示录制、链接策略、架构选型等），**直接给出建议**。除非用户明确要求分析代码，否则不要扫描代码库。

## Key Conventions

- **C++20** standard
- All asset paths are relative to the project root (e.g., `assets/shaders/PBR.glsl`)
- Scene files are YAML with `.scene` extension in `assets/scenes/`
- Shaders are raw GLSL files in `assets/shaders/` — vertex and fragment combined in one file, separated by `#type vertex` / `#type fragment` / `#type compute` pragmas
- New components must be registered with the reflection system (`ENGINE_COMPONENT` macro in `ComponentRegistry.cpp`, `REGISTER_COMPONENT_*` macros in the same file) to appear in the editor and serialize correctly
- Platform-specific code lives in `Engine/Platform/` (currently `OpenGL/`, `Vulkan/`, and `CUDA/`)
- `Ref<T>` is `std::shared_ptr<T>`, `Scope<T>` is `std::unique_ptr<T>` (defined in `Core/Base.h`)
- **Scene 架构**：Scene 为 façade，复杂逻辑委托给 SceneRuntimeCoordinator / SceneHierarchyService / WorldTransformService，运行时资源由 RuntimeStore 管理
- **Editor 架构**：EditorLayer 是主协调器，职责分散到 EditorSceneSession / EditorShell / EditorPanelCoordinator / EditorRenderController / EditorViewportController / EditorSelectionGizmoController + UndoSystem
- **Vulkan 后端**：已合并主分支，实现位于 `Engine/Platform/Vulkan/`，通过 `vs2022-vulkan` preset + `Editor.exe --vulkan` 启用
- **CUDA sidecar**：粒子和 SPH 管线有 CUDA 加速路径，含错误检测 + 全局中毒回退到 OpenGL Compute；默认关闭，可用 `vs2022-cuda` 或 `vs2022-benchmark` preset 启用（后者同时启用 Vulkan，输出到 `build-benchmark/`）
- **网络同步**：`Engine/src/Network/` 共享 TCP 协议与客户端/服务端传输；`Server` 为 60Hz 无头权威服务器，Editor 客户端以快照插值渲染。当前仅面向局域网双玩家演示，不含预测/回滚或流体同步；详见 `docs/server/architecture.md`
- **Remotion 视频工程**：独立于 CMake/C++ 构建，使用 npm 管理依赖；修改后运行 `npm run lint`，用法与渲染约定见 `remotion/README.md`

## License

LGPL v2.1+（FFmpeg 以 DLL 动态链接）

## CI/CD

GitHub Actions 当前构建矩阵为 Windows `default` 与 `vs2022-vulkan`、Linux `linux-default` 与 `linux-sanitize`。Windows/Linux 构建包含 `Server`；Linux sanitizer 任务构建 `EngineTests` 并运行 CTest。`CMakePresets.json` 虽保留 `macos-default`，当前 CI 不执行 macOS 构建，也没有 CodeQL 工作流。

## Development Workflow

Always follow this workflow:

1. Explore relevant files
2. Plan the change
3. Implement a small step
4. Create git commit
5. Build and verify
6. Continue
7. **必要时 `/checkpoint`**：phase 收尾或踩了非显而易见的坑后，调用 `/checkpoint` skill 沉淀决策/陷阱/技术选型到对应位置（任务 SPEC.md / 全局 MEMORY.md / `docs/decisions/` ADR）。不是每次 commit 都要做——只在有"非显而易见的选择"或"长期教训"时触发。

**频繁提交：** 每完成一个小步骤就立即 `git commit`，保存快照。不要攒一大堆改动再提交。

**提交前格式化：** 提交代码之前，对本次修改的 C/C++ 源文件（`.h`/`.cpp`）运行 clang-format，确保代码风格一致。不要格式化 `vendor/` 下的第三方代码。项目根目录的 `.clang-format` 会被自动识别。

Windows 下 clang-format 的具体路径见 `CLAUDE.local.md`。
