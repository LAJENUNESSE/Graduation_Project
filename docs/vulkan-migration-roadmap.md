# Vulkan 后端迁移路线图

> **分支**: `feature/vulkan-backend`
> **状态**: 探索性重构，不影响 main 分支
> **目标**: 将 OpenGL 4.3 渲染后端逐步迁移到 Vulkan 1.2+

---

## 现状分析

### 当前架构

| 层 | 说明 |
|----|------|
| `RendererAPI` (抽象) | 已有 `API::Vulkan = 2` 枚举预留 |
| `RenderCommand` (静态转发) | 所有调用经 `s_RendererAPI->xxx()` |
| `Engine/Platform/OpenGL/` | 19 个文件：Buffer/Shader/Texture/Framebuffer/VAO/SSBO/UBO/RendererAPI |
| `Engine/src/Renderer/` | 49 个文件，其中 **9 个文件直接 `#include <glad/gl.h>`** |
| `assets/shaders/` | 37 个 GLSL 文件（vertex+fragment+compute 合并格式） |

### 抽象泄漏清单（必须先修）

以下文件在 Platform 层外直接调用 `gl*` 函数：

| 文件 | 泄漏内容 |
|------|----------|
| `SceneRenderer.cpp` | SSAO 噪声纹理 `glGenTextures`/`glTexImage2D`/`glTexParameteri` |
| `FluidRenderer.cpp` | blend state 保存恢复 `glIsEnabled`/`glDisable`/`glColorMask` |
| `FluidSystemGPU.cpp` | (待确认) |
| `ParticleSystemGPU.cpp` | `glDeleteSync`/`glGenBuffers`/`glBindBuffer`/`glBufferData`/异步回读 |
| `SpatialHashGrid.cpp` | (待确认) |
| `RendererCapabilities.cpp` | `glGetString`/`glGetIntegerv` 能力查询 |
| `SkyboxSystem.cpp` | (待确认) |
| `GrassRenderSystem.cpp` | (待确认) |
| `Window.cpp` | GLFW + GL context 初始化 |

---

## 迁移阶段

### Phase 1: 封堵抽象泄漏 ✅ 已完成

**目标**: 把所有 `gl*` 调用收敛到 `Engine/Platform/` 内，`Engine/src/` 零 GL 依赖。

**方法**:
- 新增 `RendererAPI` / `RenderCommand` 方法覆盖泄漏的 GL 调用
- 将 `glGenTextures` 等封装为 `Texture2D::CreateRaw()` 或类似抽象
- 粒子/流体的异步回读封装为 `ShaderStorageBuffer::AsyncReadback()`
- 能力查询封装为 `RendererCapabilities` 静态接口
- blend/depth/scissor state 保存恢复封装为 `RenderCommand::PushState()`/`PopState()`

**验收标准**: `Engine/src/` 下零 `#include <glad/gl.h>`，构建通过。

### Phase 2: Vulkan 基础设施 ✅ 已完成

**目标**: 窗口出现 Vulkan 上下文，能 clear 一个颜色。

**已完成内容**:
- CMake 预设 `vs2022-vulkan`（`ENGINE_ENABLE_VULKAN=ON`，链接 Vulkan SDK）
- `Engine/Platform/Vulkan/` 目录：
  - `VulkanContext.h/cpp` — Instance, Debug Messenger, Surface, Physical/Logical Device, Swapchain, CommandPool, SyncObjects, ImGui RenderPass
  - `VulkanRendererAPI.h` — 全接口 stub（Phase 3 填充）
  - 其他 stub 头文件（Buffer, Framebuffer, Shader, Texture, StorageBuffer, UniformBuffer）
- GLFW 窗口 `glfwCreateWindowSurface()` 可选路径（`Window.cpp` + `GraphicsContext::Create`）
- 运行时 `--vulkan` 命令行参数 → `RendererAPI::SetAPI(Vulkan)` + `RenderCommand` 延迟初始化
- 所有工厂函数（Shader/Texture/VertexArray/IBLGenerator/GPUAsyncReadback 等）添加 `case Vulkan` 安全返回
- ImGui Vulkan 后端初始化（`ImGui_ImplVulkan_Init` + ImGui RenderPass）
- **验收**: `Editor.exe --vulkan` 启动显示矢车菊蓝清屏，swapchain resize 正常，干净退出

### Phase 3: VulkanRendererAPI 核心 ✅ 已完成

**目标**: 实现 `RendererAPI` 接口的 Vulkan 版本。

- `VulkanRendererAPI.h/cpp` — Init/Clear/SetViewport/Draw*/SetDepthTest/...
- ✅ Draw 路径补齐（最小骨架）：`DrawArrays` / `DrawArraysInstanced` / `DrawIndexed(fallback)` / `DrawLines` 已入队到 `VulkanContext`
- ✅ Debug Draw 线段路径：`QueueDrawLines` + `VK_PRIMITIVE_TOPOLOGY_LINE_LIST` pipeline
- ✅ `VulkanCommandBuffer.h/cpp` — 命令录制抽象（最小骨架已接入 `VulkanContext::SwapBuffers()`）
- ✅ `VulkanRenderPass.h/cpp` — 基础 render pass 创建（最小 color-only 工厂已接入 ImGui/Debug pass）
- ✅ `VulkanPipeline.h/cpp` — Graphics pipeline 创建（PSO）（最小工厂已接入 debug pipeline）
- ✅ `VulkanSynchronization.h/cpp` — Fence/Semaphore 管理（最小工厂已接入 Create/Cleanup）

### Phase 4: Shader 管线

**目标**: 现有 GLSL 着色器能被 Vulkan 消费。

- ✅ 集成 `glslang` 或 `shaderc` 做运行时 GLSL→SPIR-V 编译（最小 `shaderc` 可选链路已接入，缺失时安全降级）
- ✅ 修改 `Shader::Create()` 工厂：根据 API 选择 OpenGL/Vulkan 路径（Vulkan 分支已接入）
- ✅ `VulkanShader.h/cpp` — SPIR-V 模块创建 + 反射（descriptor set layout）（最小骨架 + 运行时编译接入，模块创建/反射待后续）
- ✅ Shader pragma 解析（`#type vertex/fragment/compute`）复用现有逻辑

### Phase 5: 资源抽象 ⬅️ 当前

**目标**: Buffer/Texture/Framebuffer 的 Vulkan 实现。

- ✅ `VulkanBuffer.h/cpp` — VBO/IBO via VMA（最小创建/上传/销毁路径已接入）
- ✅ `VulkanAllocator.h/cpp` — VMA allocator 生命周期接入 `VulkanContext`
- ✅ `VulkanStorageBuffer` — SSBO 等价（6 种构造变体 + SetData/GetData/ClearToZero via VMA）
- ✅ `VulkanUniformBuffer` — UBO via VMA（构造/析构/SetData）
- ✅ `VulkanTexture2D` — VMA Image + ImageView + Sampler + staging upload（5 种构造变体）
- ⬜ `VulkanTextureCubemap` — stub（warn only）
- ✅ `VulkanFramebuffer` — VMA color/depth images + VkRenderPass + VkFramebuffer（ReadPixel/MSAA stub）

### Phase 6: ImGui 集成 (部分完成)

**目标**: Editor UI 在 Vulkan 下正常显示。

- ✅ `ImGuiLayer` 根据后端 API 选择 `imgui_impl_opengl3` / `imgui_impl_vulkan` 初始化路径
- ✅ ImGui RenderPass + 自动 descriptor pool 创建
- ⬜ `VulkanContext::RenderImGui()` 实际渲染 ImGui draw data（当前 stub）
- ⬜ ImGui viewports 支持

### Phase 7: Compute 迁移

**目标**: 粒子/流体/SPH 的 compute pipeline 在 Vulkan 下运行。

- `VulkanComputePipeline.h/cpp`
- SSBO 绑定 → Descriptor Set 绑定
- `DispatchCompute` → `vkCmdDispatch`
- Memory barrier → Pipeline barrier

---

## 依赖管理

需要新增的第三方库：

| 库 | 用途 | 引入方式 |
|----|------|----------|
| Vulkan SDK / vulkan-headers | Vulkan API | vcpkg |
| VMA (VulkanMemoryAllocator) | GPU 内存管理 | vcpkg 或 header-only |
| glslang / shaderc | GLSL→SPIR-V 编译 | vcpkg |
| volk (可选) | Vulkan 函数加载 | vcpkg 或 header-only |

---

## 里程碑

| 里程碑 | 内容 | 预估 |
|--------|------|------|
| M0 | 抽象泄漏归零 | 1~2 天 |
| M1 | Vulkan 窗口 + clear color | 3~5 天 |
| M2 | 三角形渲染 | 2~3 天 |
| M3 | PBR mesh 渲染 | 1~2 周 |
| M4 | 完整场景渲染 | 2~3 周 |
| M5 | Compute + 粒子/流体 | 2~3 周 |
| M6 | 功能对等 | 持续 |
