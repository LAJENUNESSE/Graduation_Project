---
paths:
  - "Engine/**"
---

# Engine

静态库目标，构建产物被 Editor 和 Sandbox 链接。

## 目录结构

| 目录 | 职责 |
|------|------|
| `src/Core/` | 应用主循环、Layer、事件系统、输入、窗口、CrashHandler |
| `src/Renderer/` | 渲染抽象层、多 Pass 管线、材质、粒子、流体、IBL、SPHKernelMath |
| `src/Scene/` | ECS（EnTT）、组件、渲染系统、façade 架构 + 服务层 |
| `src/Reflection/` | 编译期反射宏、组件注册、自动序列化/Inspector |
| `src/Asset/` | SlotMap 资源池、异步加载、热重载 |
| `src/Physics/` | Bullet3 物理 + 自研碰撞（含 CollisionMath / SDFMath 独立模块） |
| `src/Audio/` | OpenAL 音频引擎 |
| `src/Media/` | FFmpeg 视频解码（RTSP/文件） |
| `src/Terrain/` | 高度图地形网格生成 |
| `src/Script/` | NativeScript 基类与注册表 |
| `src/Events/` | 事件类型定义（窗口、键盘、鼠标、应用） |
| `src/Debug/` | 调试工具（PerformanceMonitor、ProfileTimer、GPUTimerQuery） |
| `src/ImGui/` | ImGui 后端集成与辅助 |
| `Platform/OpenGL/` | OpenGL 4.3 具体实现 |
| `Platform/CUDA/` | CUDA compute sidecar（粒子/SPH），含错误检测 + 全局中毒回退（CudaPoisonState 可独立测试） |

## 单元测试（tests/）

基于 GoogleTest v1.14，通过 `EngineTestCore` 静态库避免链接完整引擎依赖：

| 测试文件 | 覆盖模块 |
|----------|----------|
| TestCollisionMath | Physics/CollisionMath（Sphere/AABB/OBB/SphereOBB） |
| TestSDFMath | Physics/SDFMath（BoxSDF/SphereSDF） |
| TestSceneHierarchy | Scene/SceneHierarchyService |
| TestWorldTransform | Scene/WorldTransformService |
| TestSceneEntityIndex | Scene/SceneEntityIndex |
| TestCommandHistory | Editor/CommandHistory |
| TestSPHKernel | Renderer/SPHKernelMath |
| TestCudaPoisonState | CUDA/CudaPoisonState |
| TestSlotMap | Asset/SlotMap |
| TestAssetHandle | Asset/AssetHandle |
| TestUUID / TestEvents / TestTimestep | Core 基础类型 |
| TestCudaParticleTypes | CUDA/CudaParticleTypes |

构建：`cmake --build build --target EngineTests`，运行：CTest 自动发现

## 关键约定

- `Ref<T>` = `std::shared_ptr<T>`，`Scope<T>` = `std::unique_ptr<T>`（定义在 `Core/Base.h`）
- 预编译头 `engpch.h` 由 CMake 自动注入，无需手动 include
- 新组件必须走反射注册流程才能出现在编辑器和序列化中
- 平台相关代码只放在 `Platform/` 下，`src/` 层仅使用抽象接口
- CUDA 路径需保留 OpenGL Compute 回退（非 NVIDIA GPU 兼容）
- 纯数学/纯逻辑代码应提取为 header-only 或独立 .cpp，便于单元测试（如 CollisionMath、SDFMath、SPHKernelMath、CudaPoisonState）
