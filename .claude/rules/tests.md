---
paths:
  - "tests/**"
---

# Tests（单元测试）

基于 GoogleTest v1.14 的单元测试框架。

## 架构

```
tests/
├── CMakeLists.txt          — 构建配置
└── src/
    ├── TestMain.cpp              — gtest 主入口
    ├── TestAssetHandle.cpp       — Asset/AssetHandle
    ├── TestSlotMap.cpp           — Asset/SlotMap
    ├── TestSceneEntityIndex.cpp  — Scene/SceneEntityIndex
    ├── TestSceneHierarchy.cpp    — Scene/SceneHierarchyService
    ├── TestWorldTransform.cpp    — Scene/WorldTransformService
    ├── TestSDFMath.cpp           — Physics/SDFMath
    ├── TestSPHKernel.cpp         — Renderer/SPHKernelMath
    ├── TestCommandHistory.cpp    — Editor/CommandHistory
    ├── TestUUID.cpp              — Core/UUID
    ├── TestEvents.cpp            — Core/Events
    └── TestTimestep.cpp          — Core/Timestep
```

## EngineTestCore 静态库

轻量静态库，仅编译测试所需的少量 Engine 源文件，**避免链接完整 Engine 库**（OpenGL/Vulkan/OpenAL/Bullet3 等重依赖）：

包含源文件：
- `Core/UUID.cpp`, `Core/Log.cpp`
- `Scene/SceneEntityIndex.cpp`, `SceneHierarchyService.cpp`, `WorldTransformService.cpp`, `SceneCamera.cpp`
- `Editor/CommandHistory.cpp`

依赖：entt（header-only）、glm（header-only）、spdlog

## 构建与运行

```bash
# Windows
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target EngineTests

# 运行测试
./build/tests/RelWithDebInfo/EngineTests.exe
# 或通过 CTest
cd build && ctest --build-config RelWithDebInfo -R EngineTests
```

## 添加新测试

1. 创建 `tests/src/TestXxx.cpp`
2. 在 `tests/CMakeLists.txt` 的 `add_executable(EngineTests ...)` 中添加源文件
3. 如需新的 Engine 源文件，添加到 `EngineTestCore` 静态库
4. 使用 `TEST()` 或 `TEST_F()` 宏编写测试

## 设计原则

- **可测试性提取**：纯数学/纯逻辑代码从引擎类中提取为独立模块（如 SDFMath、SPHKernelMath、CommandHistory、SceneHierarchyService、WorldTransformService），不依赖 OpenGL/Vulkan 等重运行时
- **header-only 优先**：SDFMath、SPHKernelMath 等纯计算模块用 header-only 实现
- **轻量依赖**：测试可执行只链接 EngineTestCore + spdlog + entt/glm（header-only），构建快、CI 友好
