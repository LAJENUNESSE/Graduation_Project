# AGENTS.md

This file provides guidance to coding agents when working with code in this repository.

## Project Overview

3D game engine and visual editor built with C++20 / OpenGL 4.3, created as a graduation project. The codebase is in English but comments are often in Chinese.

## Build Commands

```bash
# First-time setup: initialize git submodules (vendors are submodules)
git submodule update --init --recursive
````

### Linux / VM (Ninja)

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target Editor
# Run
./build/Editor/Editor.exe
```

### Windows (VS 2022 Build Tools + vcpkg)

cmake 不在全局 PATH 中，需使用 VS Build Tools 内置路径。

> **⚠️ 禁止使用 `find`/`ls`/`where`/`which` 等命令搜索 cmake 路径。** 下方路径是唯一正确路径，直接复制使用即可。
>
> **⚠️ 禁止使用 `CMAKE="..." && "$CMAKE"` 变量模式。** MINGW Bash 中变量赋值 + `&&` + 管道会导致变量展开为空字符串（`command not found`），必须直接写完整路径。

```bash
# ── 配置 ──────────────────────────────────────────────
# 默认（推荐）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset default

# 有 Vulkan（vs2022-vulkan preset，需 Vulkan SDK 1.4+）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset vs2022-vulkan

# ── 构建（Visual Studio 生成器需指定 --config）─────────
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Editor

# Run
./build/Editor/RelWithDebInfo/Editor.exe
```

> **注意：** Linux/Ninja 输出路径无配置子目录，Windows/VS 生成器输出路径含 `RelWithDebInfo/` 子目录。
> 两个 preset 的输出目录相同（`build/`），切换 preset 后需重新配置但构建/运行路径不变。

**Build targets:** `Engine` (static lib), `Editor` (exe), `Sandbox` (exe)

**Run:** exe 启动时会自动检测项目根目录，因此可从任意目录启动。

## Python / uv Environment

项目中的 Python 脚本统一使用 **uv** 管理依赖与虚拟环境，不要直接向系统 Python 安装依赖，也不要使用裸 `pip install`。

### Windows uv 位置

```text
C:\Users\liu69\.local\bin\uv.exe
```

当前已安装版本：`uv 0.11.17`。`uv` 已加入 `PATH`，通常直接执行 `uv` 即可；若当前 Shell 无法识别，再使用上面的绝对路径，不要重新搜索或重复安装。

论文绘图环境位于 `docs/thesis/figures/scripted/`，其中 `pyproject.toml` 声明依赖，`uv.lock` 固定依赖版本，虚拟环境默认创建在该目录的 `.venv/` 中。

```powershell
# 同步锁定的依赖并创建/更新 .venv
cd docs/thesis/figures/scripted
uv sync

# 在项目环境中运行脚本
uv run python scripts/generate_formula_figures.py
uv run python scripts/generate_method_figures.py

# 从仓库根目录复用该环境运行其他 Python 脚本
uv run --project docs/thesis/figures/scripted python benchmark/plot_results.py --help
```

Python 依赖管理约定：

* 新增依赖使用 `uv add <package>`，开发依赖使用 `uv add --dev <package>`。
* 修改依赖后必须同时提交 `pyproject.toml` 和 `uv.lock`。
* `.venv/`、缓存和临时文件不得提交到 Git。
* 可复现脚本优先通过 `uv run` 执行；不要假定全局 Python 已安装项目所需的包。

## Architecture

| 目录                      | 职责                                                                                                                        |
| ------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `Editor/`                 | 可视化编辑器（exe），分层协调式架构（EditorLayer → 控制器/面板）                                                            |
| `Engine/`                 | 引擎静态库（Core, Renderer, Scene, Reflection, Asset, Physics, Audio, Media, Terrain, Script, Events, Debug, ImGui）        |
| `Engine/Platform/OpenGL/` | OpenGL 4.3 具体实现（默认后端）                                                                                             |
| `Engine/Platform/Vulkan/` | Vulkan 1.2+ 具体实现（可选，`--vulkan` 命令行启用）                                                                         |
| `Engine/Platform/CUDA/`   | CUDA compute sidecar（粒子/SPH，需 `ENGINE_ENABLE_CUDA=ON`），含错误检测 + 全局中毒回退                                     |
| `vendor/`                 | 第三方库（glfw, glm, spdlog, entt, yaml-cpp, imguizmo, stb_image, imgui, assimp, bullet3）                                 |
| `assets/`                 | 着色器(.glsl ×37)、模型、纹理、场景(.scene = YAML)                                                                          |
| `docs/`                   | 技术调研报告、竞赛文档、论文、性能分析、Vulkan 迁移等                                                                       |

各模块的详细约定见 `.claude/rules/` 下的路径限定规则文件。

## Language

* 所有回答、解释、注释均使用**简体中文**。

## Working Style

* **遇到不确定的信息时必须主动向用户提问，禁止猜测或脑补。** 宁可多问一句，也不要基于假设给出错误结论。
* 这包括但不限于：运行场景参数（粒子数量、迭代次数等）、用户意图、项目背景、复现步骤等。
* **修复 bug 时，必须实施实际的代码修复。** 不要止步于产出计划、验证文档或分析报告。完整流程：验证 bug → 实施修复 → 构建验证 → 提交。
* **当用户请求建议或意见**（演示录制、链接策略、架构选型等），**直接给出建议**。除非用户明确要求分析代码，否则不要扫描代码库。

## Key Conventions

* **C++20** standard, MSVC on Windows with `/utf-8` flag for Chinese string literals
* All asset paths are relative to the project root (e.g., `assets/shaders/PBR.glsl`)
* Scene files are YAML with `.scene` extension in `assets/scenes/`
* Shaders are raw GLSL files in `assets/shaders/` — vertex and fragment combined in one file, separated by `#type vertex` / `#type fragment` / `#type compute` pragmas
* New components must be registered with the reflection system (`ENGINE_COMPONENT` macro in `ComponentRegistry.cpp`, `REGISTER_COMPONENT_*` macros in the same file) to appear in the editor and serialize correctly
* Platform-specific code lives in `Engine/Platform/` (currently `OpenGL/`, `Vulkan/`, and `CUDA/`)
* `Ref<T>` is `std::shared_ptr<T>`, `Scope<T>` is `std::unique_ptr<T>` (defined in `Core/Base.h`)
* **Scene 架构**：Scene 为 façade，复杂逻辑委托给 SceneRuntimeCoordinator / SceneHierarchyService / WorldTransformService，运行时资源由 RuntimeStore 管理
* **Editor 架构**：EditorLayer 是主协调器，职责分散到 EditorSceneSession / EditorShell / EditorPanelCoordinator / EditorRenderController / EditorViewportController / EditorSelectionGizmoController + UndoSystem
* **Vulkan 后端**：已合并主分支，35 个文件在 `Engine/Platform/Vulkan/`，通过 `vs2022-vulkan` preset + `Editor.exe --vulkan` 启用
* **CUDA sidecar**：粒子和 SPH 管线有 CUDA 加速路径，含错误检测 + 全局中毒回退到 OpenGL Compute（仓库预设不含 CUDA）

## License

LGPL v2.1+（FFmpeg 以 DLL 动态链接）

## CI/CD

GitHub Actions — 多预设矩阵构建（Windows `default`, Linux `linux-default` + `linux-sanitize`, macOS `macos-default`）+ CodeQL 安全分析

## Development Workflow

Always follow this workflow:

1. Explore relevant files
2. Plan the change
3. Implement a small step
4. Create git commit
5. Build and verify
6. Continue

**频繁提交：** 每完成一个小步骤就立即 `git commit`，保存快照。不要攒一大堆改动再提交。

### Git Commit 编写要求

提交主题统一采用 Conventional Commits 格式：

```text
<type>(<scope>): <简体中文摘要>
```

* `type` 使用小写英文，常用值：`feat`、`fix`、`docs`、`refactor`、`perf`、`test`、`build`、`ci`、`chore`、`revert`。
* `scope` 使用小写英文并对应实际模块，例如 `cuda`、`benchmark`、`renderer`、`editor`、`thesis`；无法准确归类时可省略。
* 摘要使用简体中文，直接说明本次提交完成的动作，不加句号，不写“更新代码”“修复问题”等模糊描述。
* 一次提交只处理一个逻辑变更；禁止混入无关格式化、临时文件或用户已有修改。
* 提交前检查 `git diff --check` 和 `git status --short`，并完成与改动风险相匹配的构建、测试或脚本运行验证。
* C/C++ 文件提交前必须按下方要求执行 clang-format；生成文件应与对应脚本或源数据一起提交，确保结果可复现。
* 若变更需要正文说明，在空行后的 commit body 中写清“为什么这样改”和验证方式，不要只复述 diff。

示例：

```text
feat(cuda): 增加 SPH 粒子计算旁路
fix(benchmark): 拒绝越界的流体状态结果
docs(thesis): 补充可复现的论文技术图
```

**提交前格式化：** 提交代码之前，对本次修改的 C/C++ 源文件（`.h`/`.cpp`）运行 clang-format，确保代码风格一致。不要格式化 `vendor/` 下的第三方代码。

**⚠️ clang-format 不在全局 PATH 中**，需使用 VS Build Tools 内置路径，规则同 cmake：

```bash
# 格式化单个文件
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/bin/clang-format.exe" -i path/to/file.cpp

# 批量格式化本次 git 修改的 C/C++ 文件（排除 vendor/）
git diff --name-only --diff-filter=d HEAD | grep -E '\.(h|cpp)$' | grep -v '^vendor/' | xargs -r "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/bin/clang-format.exe" -i
```

项目根目录的 `.clang-format` 会被自动识别，无需额外指定。
