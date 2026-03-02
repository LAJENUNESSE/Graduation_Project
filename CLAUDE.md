# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

3D game engine and visual editor built with C++17 / OpenGL 4.3, created as a graduation project. The codebase is in English but comments are often in Chinese.

## Build Commands

```bash
# First-time setup: initialize git submodules (vendors are submodules)
git submodule update --init --recursive

# Configure (Ninja generator, or omit -G for default)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build all targets
cmake --build build

# Build a specific target
cmake --build build --target Editor
cmake --build build --target Sandbox
```

**Build targets:** `Engine` (static lib), `Editor` (exe), `Sandbox` (exe)

**Run:** `./build/Editor/RelWithDebInfo/Editor.exe` — the exe auto-detects the project root at startup so it can be launched from any directory.

## Architecture

| 目录 | 职责 |
|------|------|
| `Editor/` | 可视化编辑器（exe），链接 Engine |
| `Engine/` | 引擎静态库（Core, Renderer, Scene, Reflection, Asset, Physics, Audio, Media, Terrain） |
| `Engine/Platform/OpenGL/` | OpenGL 4.3 具体实现 |
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
- Platform-specific code lives in `Engine/Platform/` (currently only `OpenGL/`)
- `Ref<T>` is `std::shared_ptr<T>`, `Scope<T>` is `std::unique_ptr<T>` (defined in `Core/Base.h`)
