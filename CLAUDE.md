# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

C++ game engine and editor — a graduation project from Hubei University of Education. The project is in early scaffolding phase with directory structure and vendor dependencies set up but no source code yet.

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

## Repository Notes

- Git submodules must be initialized: `git submodule update --init --recursive`
- The `master` branch is the working branch; `main` is the default upstream branch
- PDF/doc/image files in the root are thesis documents (gitignored in future commits)
