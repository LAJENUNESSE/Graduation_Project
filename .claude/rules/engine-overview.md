---
paths:
  - "Engine/**"
---

# Engine

静态库目标，构建产物被 Editor 和 Sandbox 链接。

## 目录结构

| 目录 | 职责 |
|------|------|
| `src/Core/` | 应用主循环、Layer、事件系统、输入、窗口、CrashHandler（namespace，`Install()` 由 EntryPoint 调用）、FluidBenchmarkConfig |
| `src/Renderer/` | 渲染抽象层、多 Pass 管线、材质、粒子、流体、IBL、SPHKernelMath、SPHRigidBodyCollector、SpatialHashGrid |
| `src/Scene/` | ECS（EnTT）、组件、渲染系统、façade 架构 + 服务层 |
| `src/Reflection/` | 编译期反射宏、组件注册、自动序列化/Inspector |
| `src/Asset/` | SlotMap 资源池、异步加载、热重载 |
| `src/Physics/` | Bullet3 物理（生产）+ PhysicsDebugDraw + SDFMath（header-only） |
| `src/Audio/` | OpenAL 音频引擎 |
| `src/Media/` | FFmpeg 视频解码（RTSP/文件） |
| `src/Terrain/` | 高度图地形网格生成 |
| `src/Script/` | NativeScript 基类与注册表 |
| `src/Events/` | 事件类型定义（窗口、键盘、鼠标、应用） |
| `src/Debug/` | 调试工具（PerformanceMonitor、ProfileTimer、GPUTimerQuery） |
| `src/ImGui/` | ImGui 后端集成与辅助（OpenGL/Vulkan 双后端分派） |
| `Platform/OpenGL/` | OpenGL 4.3 具体实现（默认后端） |
| `Platform/Vulkan/` | Vulkan 1.2+ 具体实现（可选，39 文件，`--vulkan` 命令行启用） |

## 后端选择

- **默认**: OpenGL 4.3，`Editor.exe` 直接启动
- **Vulkan**: 需 `vs2022-vulkan` CMake preset 启用 `ENGINE_ENABLE_VULKAN=ON`，运行时 `Editor.exe --vulkan` 切换
- 两后端共存：`RendererAPI::SetAPI(API::OpenGL | API::Vulkan)` 决定 `Shader/Texture/VertexArray/IBLGenerator/...` 工厂分派
- Vulkan 后端已合并主分支，详见 `docs/vulkan-migration/SPEC.md`（事实源）+ `docs/vulkan-migration-roadmap.md`（阶段定义）

## 单元测试（tests/）

基于 GoogleTest v1.14，通过 `EngineTestCore` 静态库避免链接完整引擎依赖：

| 测试文件 | 覆盖模块 |
|----------|----------|
| TestSDFMath | Physics/SDFMath（BoxSDF/SphereSDF） |
| TestSceneHierarchy | Scene/SceneHierarchyService |
| TestWorldTransform | Scene/WorldTransformService |
| TestSceneEntityIndex | Scene/SceneEntityIndex |
| TestCommandHistory | Editor/CommandHistory |
| TestSPHKernel / TestSPHCommon | Renderer/SPHKernelMath, SPHCommon |
| TestFluidBenchmarkConfig / TestFluidBenchmarkData | Benchmark 配置与数据结构 |
| TestCudaParticleTypes / TestCudaPoisonState | CUDA 粒子类型 + 中毒回退状态机（CPU 侧） |
| TestSlotMap | Asset/SlotMap |
| TestAssetHandle | Asset/AssetHandle |
| TestUUID / TestEvents / TestTimestep | Core 基础类型 |

构建：`cmake --build build --target EngineTests`，运行：CTest 自动发现

## 关键约定

- `Ref<T>` = `std::shared_ptr<T>`，`Scope<T>` = `std::unique_ptr<T>`（定义在 `Core/Base.h`）
- 预编译头 `engpch.h` 由 CMake 自动注入，无需手动 include
- 新组件必须走反射注册流程才能出现在编辑器和序列化中
- 平台相关代码只放在 `Platform/` 下，`src/` 层仅使用抽象接口（Phase 1 已封堵 `gl*` 泄漏）
- 纯数学/纯逻辑代码应提取为 header-only 或独立 .cpp，便于单元测试（如 SDFMath、SPHKernelMath、CommandHistory）
- Vulkan 路径下新增资源必须经 VMA，禁止裸 `vkAllocateMemory`
