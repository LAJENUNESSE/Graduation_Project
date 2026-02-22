# Repository Guidelines

## Project Structure & Module Organization
- `Engine/`: core engine library.
- `Engine/src/Core`: application loop, window, input, logging, layer stack.
- `Engine/src/Renderer`: renderer abstractions (buffers, shaders, textures, framebuffer, camera).
- `Engine/src/ImGui`: ImGui integration layer.
- `Engine/Platform/OpenGL`: OpenGL backend implementations.
- `Sandbox/`: runnable demo app (`Sandbox/src/SandboxApp.cpp`) and shader assets (`Sandbox/assets/shaders`).
- `Editor/assets/fonts`: runtime font resources used by ImGui.
- `vendor/`: third-party dependencies (GLFW, GLAD, ImGui, GLM, spdlog, etc., mostly as submodules).
- `build/`: generated build artifacts (do not commit).

## Build, Test, and Development Commands
- Configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- Build: `cmake --build build -j`
- Run demo: `./build/Sandbox/Sandbox`
- Rebuild after CMake changes: `cmake -S . -B build && cmake --build build -j`
- Run tests: `ctest --test-dir build --output-on-failure`
  - Note: the repository currently has no registered tests, so `ctest` reports no tests found.

## Coding Style & Naming Conventions
- Language: C++17 (`CMAKE_CXX_STANDARD 17`).
- Formatting: use `.clang-format` (LLVM-based, 4-space indent, Allman braces, 120-column limit, sorted includes).
- Naming patterns:
  - Types/classes: `PascalCase` (e.g., `OpenGLShader`).
  - Methods/functions: `PascalCase` (e.g., `SetViewport`, `OnUpdate`).
  - Members: `m_` prefix (e.g., `m_RendererID`).
  - Statics: `s_` prefix (e.g., `s_Instance`).
  - Macros: `ENGINE_*` / upper snake case.

## Testing Guidelines
- Add new tests with CTest integration in CMake so `ctest` can discover them.
- Prefer small focused tests per subsystem (Core, Renderer math/state, resource lifecycle).
- Suggested naming: `<Module>NameTests.cpp` (example: `RendererFramebufferTests.cpp`).
- For rendering/UI changes, include manual verification notes (resize behavior, input handling, viewport rendering).

## Commit & Pull Request Guidelines
- Current history uses concise milestone-style subjects (example: `Phase 0+1 complete: ...`).
- Keep commit titles short, imperative, and scoped (example: `Renderer: fix framebuffer resize timing`).
- PRs should include:
  - What changed and why.
  - Build result (`cmake --build build`).
  - Testing evidence (`ctest` and/or manual checks).
  - Screenshots/GIFs for Sandbox or ImGui viewport/UI changes.
