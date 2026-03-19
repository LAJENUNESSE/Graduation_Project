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
    ├── TestCollisionMath.cpp    — Physics/CollisionMath
    ├── TestSDFMath.cpp          — Physics/SDFMath
    ├── TestSceneHierarchy.cpp   — Scene/SceneHierarchyService
    ├── TestWorldTransform.cpp   — Scene/WorldTransformService
    ├── TestSceneEntityIndex.cpp — Scene/SceneEntityIndex
    ├── TestCommandHistory.cpp   — Editor/CommandHistory
    ├── TestSPHKernel.cpp        — Renderer/SPHKernelMath
    ├── TestCudaPoisonState.cpp  — CUDA/CudaPoisonState
    ├── TestSlotMap.cpp          — Asset/SlotMap
    ├── TestAssetHandle.cpp      — Asset/AssetHandle
    ├── TestUUID.cpp             — Core/UUID
    ├── TestEvents.cpp           — Core/Events
    ├── TestTimestep.cpp         — Core/Timestep
    └── TestCudaParticleTypes.cpp — CUDA/CudaParticleTypes
```

## EngineTestCore 静态库

轻量静态库，仅编译测试所需的少量 Engine 源文件，**避免链接完整 Engine 库**（OpenGL/OpenAL/Bullet3 等重依赖）：

包含源文件：
- `Core/UUID.cpp`, `Core/Log.cpp`
- `Scene/SceneEntityIndex.cpp`, `SceneHierarchyService.cpp`, `WorldTransformService.cpp`, `SceneCamera.cpp`
- `Physics/CollisionMath.cpp`
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

- **可测试性提取**：纯数学/纯逻辑代码从引擎类中提取为独立模块（如 CollisionMath、SDFMath、SPHKernelMath、CudaPoisonState、CommandHistory），不依赖 OpenGL/CUDA 等重运行时
- **header-only 优先**：SDFMath、SPHKernelMath、CudaPoisonState 等纯计算模块用 header-only 实现
- **CUDA 兼容**：SDFMath 使用 `SDF_DEVICE` 宏兼容 `__device__` 和普通 C++
