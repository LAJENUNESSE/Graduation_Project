# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

3D game engine and visual editor built with C++20 / OpenGL 4.3, created as a graduation project. The codebase is in English but comments are often in Chinese.

## Build Commands

```bash
# First-time setup: initialize git submodules (vendors are submodules)
git submodule update --init --recursive
```

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
# ── 配置（二选一）──────────────────────────────────────
# 无 CUDA（default preset）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset default

# 有 CUDA（vs2022-cuda preset，需安装 NVIDIA CUDA Toolkit）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset vs2022-cuda

# ── 构建（Visual Studio 生成器需指定 --config）─────────
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Editor

# Run
./build/Editor/RelWithDebInfo/Editor.exe
```

> **注意：** Linux/Ninja 输出路径无配置子目录，Windows/VS 生成器输出路径含 `RelWithDebInfo/` 子目录。
> 两个 preset 的输出目录相同（`build/`），切换 preset 后需重新配置但构建/运行路径不变。

**Build targets:** `Engine` (static lib), `Editor` (exe), `Sandbox` (exe)

**Run:** exe 启动时会自动检测项目根目录，因此可从任意目录启动。

## Architecture

| 目录 | 职责 |
|------|------|
| `Editor/` | 可视化编辑器（exe），分层协调式架构（EditorLayer → 控制器/面板） |
| `Engine/` | 引擎静态库（Core, Renderer, Scene, Reflection, Asset, Physics, Audio, Media, Terrain, Script, Events, Debug, ImGui） |
| `Engine/Platform/OpenGL/` | OpenGL 4.3 具体实现 |
| `Engine/Platform/CUDA/` | CUDA compute sidecar（粒子/SPH，需 `ENGINE_ENABLE_CUDA=ON`），含错误检测 + 全局中毒回退 |
| `vendor/` | 第三方库（glfw, glad-generated, glm, entt, spdlog, imgui, imguizmo, yaml-cpp, stb_image, bullet3, assimp, tinyfiledialogs） |
| `assets/` | 着色器(.glsl ×36)、模型、纹理、场景(.scene = YAML) |
| `docs/` | 深度调研报告（CUDA 迁移、UE 反射、功能审查等） |

各模块的详细约定见 `.claude/rules/` 下的路径限定规则文件。

## Language

- 所有回答、解释、注释均使用**简体中文**。

## Working Style

- **遇到不确定的信息时必须主动向用户提问，禁止猜测或脑补。** 宁可多问一句，也不要基于假设给出错误结论。
- 这包括但不限于：运行场景参数（粒子数量、迭代次数等）、用户意图、项目背景、复现步骤等。

## Key Conventions

- **C++20** standard, MSVC on Windows with `/utf-8` flag for Chinese string literals
- All asset paths are relative to the project root (e.g., `assets/shaders/PBR.glsl`)
- Scene files are YAML with `.scene` extension in `assets/scenes/`
- Shaders are raw GLSL files in `assets/shaders/` — vertex and fragment combined in one file, separated by `#type vertex` / `#type fragment` / `#type compute` pragmas
- New components must be registered with the reflection system (macros in header, `REGISTER_COMPONENT_*` in a .cpp) to appear in the editor and serialize correctly
- Platform-specific code lives in `Engine/Platform/` (currently `OpenGL/` and `CUDA/`)
- `Ref<T>` is `std::shared_ptr<T>`, `Scope<T>` is `std::unique_ptr<T>` (defined in `Core/Base.h`)
- **Scene 架构**：Scene 为 façade，复杂逻辑委托给 SceneRuntimeCoordinator / SceneHierarchyService / WorldTransformService，运行时资源由 RuntimeStore 管理
- **Editor 架构**：EditorLayer 是主协调器，职责分散到 EditorSceneSession / EditorShell / EditorPanelCoordinator / EditorRenderController / EditorViewportController + UndoSystem
- **CUDA sidecar**：粒子和 SPH 管线有 CUDA 加速路径，含错误检测 + 全局中毒回退到 OpenGL Compute

## License

LGPL v2.1+（FFmpeg 以 DLL 动态链接）

## CI/CD

GitHub Actions — CMake 构建 + CodeQL 安全分析

## Development Workflow

Always follow this workflow:

1. Explore relevant files
2. Plan the change
3. Implement a small step
4. Create git commit
5. Build and verify
6. Continue

**频繁提交：** 每完成一个小步骤就立即 `git commit`，保存快照。不要攒一大堆改动再提交。

**提交前格式化：** 提交代码之前，对本次修改的 C/C++ 源文件（`.h`/`.cpp`）运行 clang-format，确保代码风格一致。不要格式化 `vendor/` 下的第三方代码。

**⚠️ clang-format 不在全局 PATH 中**，需使用 VS Build Tools 内置路径，规则同 cmake：

```bash
# 格式化单个文件
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/bin/clang-format.exe" -i path/to/file.cpp

# 批量格式化本次 git 修改的 C/C++ 文件（排除 vendor/）
git diff --name-only --diff-filter=d HEAD | grep -E '\.(h|cpp)$' | grep -v '^vendor/' | xargs -r "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/bin/clang-format.exe" -i
```

项目根目录的 `.clang-format` 会被自动识别，无需额外指定。