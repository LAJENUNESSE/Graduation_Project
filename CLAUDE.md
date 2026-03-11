# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

3D game engine and visual editor built with C++17 / OpenGL 4.3, created as a graduation project. The codebase is in English but comments are often in Chinese.

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

> **⚠️ 禁止使用 `find`/`ls`/`where`/`which` 等命令搜索 cmake 路径。** 下方路径是唯一正确路径，直接复制使用即可：

```bash
# ⚠️ 每条命令必须和变量赋值写在同一行（Claude Code 每次调用是独立 shell，变量不跨调用保留）

# ── 配置（二选一）──────────────────────────────────────
# 无 CUDA（default preset）
CMAKE="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" && "$CMAKE" --preset default

# 有 CUDA（vs2022-cuda preset，需安装 NVIDIA CUDA Toolkit）
CMAKE="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" && "$CMAKE" --preset vs2022-cuda

# ── 构建（Visual Studio 生成器需指定 --config）─────────
CMAKE="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" && "$CMAKE" --build build --config RelWithDebInfo --target Editor

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
| `Editor/` | 可视化编辑器（exe），链接 Engine |
| `Engine/` | 引擎静态库（Core, Renderer, Scene, Reflection, Asset, Physics, Audio, Media, Terrain） |
| `Engine/Platform/OpenGL/` | OpenGL 4.3 具体实现 |
| `Engine/Platform/CUDA/` | CUDA compute sidecar（粒子/SPH，需 `ENGINE_ENABLE_CUDA=ON`） |
| `vendor/` | 第三方库（glfw, glad, glm, entt, spdlog, imgui, imguizmo, yaml-cpp, stb_image, bullet3, assimp, tinyfiledialogs） |
| `assets/` | 着色器(.glsl)、模型、纹理、场景(.scene = YAML) |

各模块的详细约定见 `.claude/rules/` 下的路径限定规则文件。

## Language

- 所有回答、解释、注释均使用**简体中文**。

## Working Style

- **遇到不确定的信息时必须主动向用户提问，禁止猜测或脑补。** 宁可多问一句，也不要基于假设给出错误结论。
- 这包括但不限于：运行场景参数（粒子数量、迭代次数等）、用户意图、项目背景、复现步骤等。

## Key Conventions

- **C++17** standard, MSVC on Windows with `/utf-8` flag for Chinese string literals
- All asset paths are relative to the project root (e.g., `assets/shaders/PBR.glsl`)
- Scene files are YAML with `.scene` extension in `assets/scenes/`
- Shaders are raw GLSL files in `assets/shaders/` — vertex and fragment combined in one file, separated by `#type vertex` / `#type fragment` / `#type compute` pragmas
- New components must be registered with the reflection system (macros in header, `REGISTER_COMPONENT_*` in a .cpp) to appear in the editor and serialize correctly
- Platform-specific code lives in `Engine/Platform/` (currently `OpenGL/` and `CUDA/`)
- `Ref<T>` is `std::shared_ptr<T>`, `Scope<T>` is `std::unique_ptr<T>` (defined in `Core/Base.h`)

## Development Workflow

Always follow this workflow:

1. Explore relevant files
2. Plan the change
3. Implement a small step
4. Create snapshot
5. Run tests
6. Continue