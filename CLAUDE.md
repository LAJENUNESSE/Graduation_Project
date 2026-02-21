# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

C++ game engine and editor — a graduation project from Hubei University of Education.
Phase 0 (Core/Events/Window) and Phase 1 (Renderer/ImGui viewport) are complete.
Currently working on Phase 2: Scene/ECS and Editor.

## Architecture

Three CMake targets are planned:

- **Engine/** — Core engine library
  - `src/Core/` — Application loop, window, input, logging
  - `src/Renderer/` — Rendering abstractions (buffers, shaders, framebuffers, cameras)
  - `src/Scene/` — ECS-based scene graph (uses EnTT)
  - `src/Events/` — Event system
  - `src/Math/` — Math utilities (supplements GLM)
  - `src/ImGui/` — ImGui integration layer
  - `src/Resource/` — Asset loading and management
  - `Platform/OpenGL/` — OpenGL-specific renderer implementation

- **Editor/** — Visual editor application
  - `src/Panels/` — Editor UI panels (scene hierarchy, inspector, viewport, etc.)
  - `assets/` — Editor-specific assets

- **Sandbox/** — Test/demo application
  - `assets/shaders/` — GLSL shaders
  - `assets/models/` — 3D models
  - `assets/textures/` — Textures
  - `assets/scenes/` — Scene files (likely YAML via yaml-cpp)

## Vendor Dependencies

All in `vendor/`, managed as git submodules (plus glad):

| Library | Purpose |
|---------|---------|
| glfw | Windowing and input |
| glad | OpenGL function loader |
| glm | Math (vectors, matrices, transforms) |
| entt | Entity-Component-System |
| spdlog | Logging |
| imgui (docking branch) | Editor/debug UI |
| imguizmo | 3D manipulation gizmos |
| yaml-cpp | Serialization (scenes, config) |
| stb_image | Image loading |

## Build System

CMake-based, using Ninja generator. Build workflow:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
```

## Development Notes

- 开发阶段默认窗口大小使用 **1280x720**（WindowProps 默认值）
- VM 环境：VMware SVGA3D, OpenGL 4.3 Core Profile, Mesa 25.2.8
- GLFW 已禁用 Wayland，仅使用 X11
- GLAD2 生成的头文件使用 `<glad/gl.h>`，不是 `<glad/glad.h>`
- 中文字体：Editor/assets/fonts/NotoSansSC-Regular.ttf，在 ImGuiLayer::OnAttach() 中加载
- **游戏引擎 UI 全部使用中文**：菜单、面板标题、组件名称、属性标签等均为中文。字体文件在编译时打包到输出文件中（通过 ImGui 的 AddFontFromFileTTF 加载）

## Architecture Patterns

- Framebuffer resize 必须在 OnUpdate() 渲染之前执行，不能在 OnImGuiRender() 中执行。否则新 FBO 没有内容会闪烁。标准流程：OnImGuiRender 只记录期望尺寸 → 下一帧 OnUpdate 开头检测变化并 resize → 立刻渲染填充内容。
- Engine 编译为静态库，Renderer/ 放抽象接口，Platform/OpenGL/ 放具体实现
- 预编译头 engpch.h 包含所有 STL 头文件
- GLM 使用 gtx 扩展需要在 include 前 `#define GLM_ENABLE_EXPERIMENTAL`

## Code Style

Enforced via `.clang-format` (LLVM-based):
- 4-space indentation, no tabs
- 120-char line limit
- Allman brace style (opening brace on new line)
- Left-aligned pointers (`int* ptr`)
- Namespace contents are indented
- Access modifiers outdented by 4 spaces

Format code with:
```bash
clang-format -i <file>
```

## Known Limitations

- 自动化单元测试暂未添加：引擎依赖 OpenGL 图形上下文，VM 环境下无法进行无头（headless）测试。后续可考虑引入 mock 层或 EGL 无头上下文支持。

## Repository Notes

- Git submodules must be initialized: `git submodule update --init --recursive`
- The `master` branch is the working branch; `main` is the default upstream branch
- PDF/doc/image files in the root are thesis documents (gitignored in future commits)
