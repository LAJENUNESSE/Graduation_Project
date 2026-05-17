# Vulkan 后端迁移 · SPEC

> **作用**：本文件是 Vulkan 迁移分支的**事实源**。新 session 进入此分支时，第一动作是 `read SPEC.md`，无需翻 git log 拼凑进度。
> **分支**：`feature/vulkan-backend`（自 `main` 分叉 34 个 commit）
> **配套路线图**：[../vulkan-migration-roadmap.md](../vulkan-migration-roadmap.md)（阶段定义与验收标准，长期不变）
> **更新规则**：完成一个 phase 子项立即把 `- [ ]` 改成 `- [x]`；做出非显而易见的选择立即记到 "关键决策"。

---

## 1. 当前位置

- **进行中阶段**：Phase 8 — Vulkan PBR pass 接通中（cubemap + Editor 主路径 + IBL view 分派完成；DrawIndexed 真实实装待 Phase 8.2）
- **最近一次 commit**：`8f16fa0 feat(vulkan): switch Editor main path + IBL view API dispatch + frame ordering`
- **下一步**：见 [§6 Next Steps](#6-next-steps)（VulkanVertexArray + Pipeline Cache + DrawIndexed 实装 → Phase 8.2）

---

## 2. 已完成 Phase（按时间倒序）

### Phase 8 — Vulkan PBR pass 接通 ⬅️ 进行中

**已完成**
- [x] `VulkanTextureCubemap` 实装 — VMA Image (arrayLayers=6 + CUBE_COMPATIBLE) + cube ImageView + Sampler + 6 region staging（`0c6eb28`）
- [x] `VulkanIBLGenerator::Generate` 接入真实 cubemap — 删占位 envAtlas，descriptor binding=1 改 COMBINED_IMAGE_SAMPLER + cube view；IBL GLSL 双路径 `#ifdef VULKAN samplerCube` / OpenGL atlas + imageLoad（`28fcfc9`）
- [x] `EditorApp.cpp` 删 Vulkan→SmokeLayer 硬切，统一走 EditorLayer（`8f16fa0`）
- [x] `GraphicsContext::{Begin,End}RenderFrame` 抽象 + Vulkan override + `Application::Run` 主循环时序（BeginRenderFrame → OnUpdate → ImGui → EndRenderFrame → Window::OnUpdate）（`8f16fa0`）
- [x] `IBLGenerator::Get{Irradiance,Prefilter,BRDFLut}View / GetIBLSampler` 4 个 `void*` 虚函数 + `VulkanIBLGenerator` override + `SkyboxSystem` 透传 + `RenderCommand::Bind{Cubemap,Texture}View`（`8f16fa0`）
- [x] `SceneRenderer::GeometryPass` IBL 按 API 分派 ID vs View；PBR.glsl IBL 采样器 `#ifdef VULKAN` 显式 `layout(set=0, binding=6/7/8)`（`8f16fa0`）

**待 Phase 8.2**
- [ ] `VulkanRendererAPI::DrawIndexed` 真实实装 — 需新建 `VulkanVertexArray`（BufferLayout → VkVertexInputBindingDescription/AttributeDescription）+ Pipeline Cache（shader hash + render pass + vertex layout）+ DescriptorSet flush + 主帧 cmd 录制
- [ ] Vulkan path PBR mesh 真实可见（当前 DrawIndexed 走 warn-once-fallback，PBR mesh 在 Vulkan path 不渲染但不崩）

### Phase 7 — Compute 迁移 ✅

**基础设施**（已就绪）
- [x] `VulkanContext` 增加 compute queue family 查询（`16a3db0`）
- [x] `VulkanShader` 暴露 SPIR-V + `VkShaderModule` 懒创建缓存（`29ed242`）
- [x] `VulkanShader` 集成 spirv-cross 反射 descriptor binding + push constant（`18f4277`）
- [x] `VulkanDescriptor` 三层抽象（`SetLayout` / `Pool` / `Writer`）（`ff45e44`）
- [x] `VulkanPipeline::CreateCompute` 工厂（`fed450e`）
- [x] `VulkanCommandBuffer` compute 命令封装（`f547bd0`）
- [x] `VulkanBarrierUtil` — `BarrierBit::{ShaderStorage,Command,BufferUpdate,All}` → `VkPipelineStage`/`VkAccess` 映射（`6f577a5`）
- [x] `VulkanStorageBuffer::ExternalMemoryHint` 占位（CUDA 互操作，按 CUDA sidecar 迁移驱动实装，详见 D-6）（`2625fa4`）
- [x] `VulkanContext` 拆 `BeginFrame()` / `EndFrame()` / `GetCurrentFrameCommandBuffer()` + readback fence 信号化 hook（`777de43`）

**子系统迁移**
- [x] `IBLGenerator` — BRDF LUT / Irradiance / Prefilter 三 compute dispatch（`6ca3cb3`）
- [x] `GrassRenderSystem` — placement + render_args 两 pass，DrawArraysIndirect 链路（`97750ae`）
- [x] `SpatialHashGrid` — hash / prefix_sum (三 pass) / scatter（`93b2e2d`）
- [x] `AsyncReadback` ring buffer — VulkanAsyncReadback 3 槽 + 独立 fence + EndFrame 零 cmd submit 信号化（`ad31c42`）
- [x] `SpatialHashGrid::BuildVulkan(cmd, ...)` 接受外部 cmd buffer + ResetFrameResources，每帧调用方录主帧 cmd（`c7b2c92`）
- [x] `ParticleSystemGPU` 非 SPH 路径 — emit / simulate / render_args 3 dispatch + 4 shader（`399bd2c`）
- [x] `FluidSystemGPU` emit/simulate — 2 dispatch + 2 shader + MeshSDFMeta 异步回读预埋（`42eb259`）
- [x] SPH 整体迁移 — 7 SPH shader + FluidSystemGPU PCISPH 8 迭代 + 粒子 SPH WCSPH 接入（`0fa0923`）
- [x] `VulkanTextureCubemap` 实装（`0c6eb28`，移到 Phase 8）

### Phase 6 — ImGui 集成 ✅

- [x] `imgui_impl_vulkan` 初始化 + RenderPass + descriptor pool（`5f0fe51`）
- [x] 多视口（Vulkan only）
- [x] `VulkanSmokeLayer` 增加 ImGui 验证窗口（`d807afe`）
- [x] `VulkanContext::RenderImGui()` 实装 — 缓存 drawData 指针，`RecordImGuiPass(cmd, imageIndex)` 录制到主帧 cmd 调 `ImGui_ImplVulkan_RenderDrawData`

### Phase 5 — 资源抽象 ✅

- [x] VMA allocator 接入 `VulkanContext`（`5f29fd9`）
- [x] `VulkanBuffer` (VBO/IBO) / `VulkanStorageBuffer` 6 种构造变体
- [x] `VulkanUniformBuffer`（`91cc2a8`）
- [x] `VulkanTexture2D` — VMA Image + view + sampler + staging upload（`95596b9`）
- [x] `VulkanFramebuffer` — color/depth images + RenderPass（`96032dd`）
- [ ] `VulkanTextureCubemap`（stub，warn only）—— 已移到 Phase 7 待办

### Phase 4 — Shader 管线 ✅
- [x] shaderc 运行时 GLSL→SPIR-V（`9f1a46e`），缺失时安全降级
- [x] `Shader::Create()` 按 API 分派
- [x] `#type vertex/fragment/compute` pragma 复用

### Phase 3 — VulkanRendererAPI 核心 ✅
- [x] Draw 路径骨架（DrawArrays / Instanced / Indexed / Lines）（`7e9b99f`）
- [x] Debug Draw 线段 pipeline（LINE_LIST）
- [x] CommandBuffer / RenderPass / Pipeline / Synchronization 最小工厂接入

### Phase 2 — Vulkan 基础设施 ✅
- [x] `Editor.exe --vulkan` 显示矢车菊蓝清屏，swapchain resize / 干净退出（`450b7ef`, `e4e1938`）
- [x] Instance / DebugMessenger / Surface / Device / Swapchain / CommandPool / SyncObjects
- [x] GLFW `glfwCreateWindowSurface()` 可选路径

### Phase 1 — 抽象泄漏归零 ✅
- [x] `Engine/src/` 下零 `#include <glad/gl.h>`（`27398fc` 终态）
- [x] SSAO 噪声、blend state、异步回读、能力查询、IBL 等全部抽象到 Platform 层

---

## 3. 关键决策（Decision Log）

> **格式**：每条决策附 **背景**（为什么不得不选）+ **结论**（选了什么）+ **影响范围**（谁会被这个决策约束）。三个月后回看时，这三项是判断"是否还成立"的依据。

### D-1：Vulkan 路径下散装 uniform 改用 `push_constant`

> **Decision**：grid_hash / grid_prefix_sum / grid_scatter / IBL 三 compute shader 在 `#ifdef VULKAN` 分支下，`default uniform` 改写为 `layout(push_constant) block`。
> **Why**：≤16 bytes 的小常量（如 cell_count、particle_count、roughness）走 push constant 比 UBO 轻量，且无需 dynamic update / descriptor set 写入。
> **How to apply**：新增 compute 迁移时，若每帧变化的 uniform ≤ 128 bytes（spec 最低保证），优先 push constant，不要新建 UBO。

### D-2：GLSL 单源双路径（OpenGL + Vulkan 共存）

> **Decision**：所有迁移的 compute shader 用 `#ifdef VULKAN` 切换，OpenGL 路径完全不动。
> **Why**：迁移分支不影响 main，必须保证 `Editor.exe`（无 `--vulkan`）行为零回归。
> **How to apply**：写 shader 改动时必须双路径并存；`VulkanShader.cpp` 在 shaderc 编译选项里显式注入 `VULKAN=1` macro（与 `GL_KHR_vulkan_glsl` 扩展自动定义形成双保险）。

### D-3：Phase 7 暂用 `BeginSingleTimeCommands` 同步提交

> **Decision**：IBL Init / RebuildGrass / SpatialHashGrid Rebuild 走"录制 → submit → waitIdle"同步链路。
> **Why**：这些路径都是低频（Init / 视角触发），主帧 stall 可接受；先把功能跑通。
> **How to apply**：**每帧执行的 dispatch**（粒子/流体 step）禁止再走 SingleTime，必须录制到主帧 command buffer。本条改造已在 Phase 7 A0/B0/B/C/D 落地（ADR-0002 选激进路线，原"Phase 7.5"独立阶段不再存在）。

### D-4：`grassCount` 同步阻塞回读

> **Decision**：`vmaMapMemory` 直接读 host-visible buffer，跳过 OpenGL 的 fence 轮询。
> **Why**：RebuildGrass 低频；fence ring 复杂度先省掉。
> **How to apply**：粒子/流体的 CPU↔GPU 回读迁移时**不要**复制此模式，必须实现 `AsyncReadback` ring buffer（多帧 in-flight + fence 轮询）。

### D-5：`SpatialHashGrid::SetExternalBuffers(...)` 显式注入

> **Decision**：Vulkan 路径要求调用方（`ParticleSystemGPU` / `FluidSystemGPU`）显式 setter 注入 particlePool / aliveList / pcisphPool。
> **Why**：Vulkan 没有 OpenGL 的全局 SSBO binding，descriptor set 必须知道具体 buffer。
> **How to apply**：粒子/流体迁移时**先**补 setter 调用，再迁移自身 compute，否则 SpatialHashGrid 在 Vulkan 路径下拿不到外部 buffer。

### D-6：`ExternalMemoryHint` 占位 — 按 CUDA sidecar 迁移驱动实装

> **Decision**：`VulkanStorageBuffer::ExternalMemoryHint::CudaInterop` 当前断言未实现。
> **Why**：CUDA-Vulkan 互操作需要 `VK_EXTERNAL_MEMORY_HANDLE_TYPE_*` + Win32/Fd handle 导出导入链路，Phase 7 主线先不引入。原标注"Phase 7.5 实装"，但激进路线（ADR-0002）已让 Phase 7.5 这个独立阶段消失，本项改为**按 CUDA sidecar 迁移触发**。
> **How to apply**：上层 API 透传该枚举即可；真正启用时机由 CUDA sidecar 迁移驱动，不挂在固定 phase 标签下。

### D-7：`VulkanIBLGenerator::GetXxxID()` 返回 0 占位

> **Decision**：`uint32_t GetIrradianceMapID()` 等接口在 Vulkan 路径返回 0。
> **Why**：现有 `IBLGenerator` 接口签名假设 OpenGL texture ID；Vulkan 需要 `VkImageView`。
> **How to apply**：Vulkan PBR pass 接入时**改接口**（新增 `GetIrradianceView()` 等），不要 cast `VkImageView` 到 uint32_t。已暴露的 `GetIrradianceView()` / `GetPrefilterView()` / `GetBRDFLutView()` 为目标接口。

### D-8：spirv-cross 取 Vulkan SDK 自带 lib，不走 vcpkg

> **Decision**：`Engine/CMakeLists.txt` 用 `find_library(SPIRV_CROSS_CORE_LIB NAMES spirv-cross-core HINTS "$ENV{VULKAN_SDK}/Lib")` 优先走 Vulkan SDK 路径，vcpkg `find_package(spirv_cross_core CONFIG QUIET)` 为 fallback。
> **Why**：vcpkg manifest 模式安装 `spirv-cross:x64-windows@1.4.335.0` 时 curl 从 GitHub 下载源码失败（error 35 SSL connect error），网络/GFW 在我们环境下不稳定。Vulkan SDK 1.4.341.1 自带完整 spirv-cross-{core,glsl,cpp,c} 头与 lib，零下载成本。
> **How to apply**：未来加 Vulkan 周边依赖（如 SPIRV-Reflect、glslang）时先看 SDK 是否自带，避开 vcpkg 网络下载。

### D-9：`RendererAPI::DispatchCompute` / `MemoryBarrier` 在 Vulkan 路径保持 stub+warn

> **Decision**：不在 `VulkanRendererAPI` 内部启动隐藏 cmd buffer 实装 OpenGL immediate-mode 风格 dispatch；迁移方各系统直接调用 `VulkanCommandBuffer::BindComputePipeline / Dispatch / Barrier`，配合 `ResolveBarrierBits()` 把 OpenGL `BarrierBit` 解析为 Vulkan 四元组。
> **Why**：OpenGL `glDispatchCompute(g)` 是 immediate 全局状态机；Vulkan 需要绑 pipeline + descriptor set + cmd buffer 上下文，强行适配会引入隐藏 cmd buffer + 提交时序歧义，比"迁移时显式重写 dispatch 链路"风险大。
> **How to apply**：粒子/SPH/流体迁移时 **不要** 调 `RenderCommand::DispatchCompute`，而是显式 lazy pipeline 初始化 + 录制 cmd buffer + barrier 序列；调用 `ResolveBarrierBits(BarrierBit::X)` 拿 stage/access 四元组传给 `cmdBuffer.MemoryBarrier(...)`。

### D-10：`VulkanContext` 拆 BeginFrame/EndFrame 以暴露主帧 cmd buffer

> **Decision**：`SwapBuffers()` 内部 `vkWaitForFences → Acquire → Begin cmd → Record → End → Submit → Present` 拆为三段：`BeginFrame()` 返回 bool 表明本帧能否录制；`EndFrame()` 完成主 submit + 信号化 readback fence + present；`GetCurrentFrameCommandBuffer()` 仅在帧内有效。`SwapBuffers` 退化为 thin wrapper（内部清屏/Debug/ImGui pass 行为零变化）。
> **Why**：粒子/流体每帧 dispatch 必须录主帧 cmd（D-3），原 SwapBuffers 完全自包含、外部无录制入口。激进路线：一次到位拆 VulkanContext，而非每帧 dispatch 退回 SingleTime。
> **How to apply**：每帧高级调用方走 `if (ctx->BeginFrame()) { cmd = ctx->GetCurrentFrameCommandBuffer(); ... record ...; ctx->RecordImGuiPass(...); ctx->EndFrame(); }`，绕过 SwapBuffers 默认清屏与 ImGui pass。SmokeLayer 继续走 SwapBuffers。

### D-11：`VulkanAsyncReadback` 3 槽 ring + 独立 fence

> **Decision**：`GPUAsyncReadback` Vulkan 实现用 3 槽 round-robin ring；每槽独立 VMA host-visible persistent-mapped staging buffer + 独立 VkFence。CopyFrom 把 vkCmdCopyBuffer 录入主帧 cmd 并调 `VulkanContext::RegisterReadbackFenceSignal(fence)`；EndFrame 在主 submit 之后追加零 cmd `vkQueueSubmit(fence)` 信号化（队列内顺序保证 copy 已完成）。接口 5 函数（CopyFrom/IsReady/GetData/Reset/IsPending）语义不变。
> **Why**：(1) 主帧单 submit 只能 signal 1 个 fence —— 不能复用 inFlightFence；(2) 3 槽避免回读对每帧节奏的 stall（粒子 counter 每帧 CopyFrom）。
> **How to apply**：调用方必须在 BeginFrame~EndFrame 之间调 CopyFrom；前 1~2 帧 IsReady 必为 false（ring 填充期）。ring 满（极端连续 3 帧未消费）回退到 vkWaitForFences 等最老槽，避免 UAF。

### D-12：`SpatialHashGrid::BuildVulkan(cmd, ...)` 接受外部 cmd + 显式 ResetFrameResources

> **Decision**：新增 public `BuildVulkan(VkCommandBuffer cmd, aliveCount, usePredictedPos)` 重载，每帧调用方录入自己主帧 cmd。原 `Build(...)` 通用入口 Vulkan 分支退化为 SingleTime wrapper（IBL/Grass 等低频调用方零修改）。pool reset 从 BuildVulkan 内部移出，调用方每帧首次 dispatch 前显式调 `ResetFrameResources()`。pool 容量从 16 扩到 64（PCISPH 8 × 3 + 余量）。
> **Why**：PCISPH 1~8 迭代同帧多次调 BuildVulkan + 原内部 Pool->Reset() = 第二次 reset 让第一次的 set 失效（已 BindDescriptorSets 录入 cmd），cmd submit 时 UAF。
> **How to apply**：每帧首次 `ResetFrameResources()` → `SetExternalBuffers(...)` → `BuildVulkan(cmd, alive, false)` → ... → 同帧后续可多次 `BuildVulkan(cmd, alive, true)` 复用同一 pool 但 alloc 新 set。

### D-13：粒子/流体每帧 Vulkan 参数分流 — UBO + push constant

> **Decision**：每帧粒子/流体 dispatch 的常量参数按"是否 >16B + 是否每 dispatch 变化"二分：每 dispatch 必变的小常量（emitCount / dt / maxParticles / seed / flags）走 `layout(push_constant)`（≤16B），emitter 状态 / boundary / SDF / 颜色梯度等大块向量参数走 UBO（binding=5 emit、binding=6 simulate）。OpenGL 路径保留原 `uniform vec3 u_EmitterPos` 等具名 uniform 不变，shader 主体用 `#define` 宏（如 `EMITTER_POS / SPEED_MIN / SIZE_START`）统一两侧 main 函数表达式。
> **Why**：push constant 最小保证 128B，但 emitter 6 vec4 = 96B 占满后再加小常量风险面大；语义上 emitter 是"每次发射周期变化"，每 dispatch 必变的才是小常量。OpenGL uniform 名保留是兼容 cpp 端 `Shader::SetInt/Float/Vec3` 调用 — 不能因 shader 改写让 cpp 端断裂。
> **How to apply**：新增每帧 Vulkan dispatch 时按此模板分流；新增 UBO 绑定从 binding=5 起，避开 SSBO 占用的 0~4 + binding 9（PCISPH）。Vulkan 路径下 UBO 由 cpp 端按 API 分支 `Create`，OpenGL 路径不 alloc。

### D-14：`MeshSDFMeta` 调试回读 Vulkan-only 切换到 `GPUAsyncReadback`

> **Decision**：Vulkan 路径 `FluidSystemGPU::Init` 创建 `m_SDFMetaReadback = GPUAsyncReadback::Create(sizeof(MeshSDFMeta))`，`UpdateVulkan` 末尾以 `m_MeshSDFMetaBuffer` 双 guard 调用 `IsPending/IsReady/GetData/CopyFrom` 五函数模式。OpenGL 路径保留原 `GetData` 同步读，零回归。
> **Why**：原同步路径在 Vulkan 下等同 `vmaMapMemory` 同步读 host-visible（违反 D-4 异步回读要求）；但 OpenGL 路径低频且工作正常，盲改面会扩大回归风险，故仅 Vulkan 切换。
> **How to apply**：未来新增 GPU→CPU 调试回读时 — Vulkan 路径强制走 `GPUAsyncReadback`，禁裸 `vmaMapMemory`；OpenGL 路径按工程量决定是否一并切换。当前 SPH 段在 Vulkan 下跳过（Commit C 接入前），所以本 commit 的 readback 调用为预埋状态，C 接通 `InitMeshSDFBuffer` 后自然生效。

### D-15：SPH 7 shader 共享统一 `SPHParams` UBO（binding=12，80B）

> **Decision**：7 个 SPH shader（density / force / pcisph_init/predict/density/force/apply）在 Vulkan 路径下共享同一份 `SPHParams` UBO 布局（5×vec4 std140 = 80B），不为每 shader 独立 UBO。`SPHParamsUBO` 字段：`GravityAndSmoothingRadius / MassDensityGasViscosity / GridParams / BoundaryParams / SDFCounts`（最后一个 .w 复用为 PCISPHDelta）。push constant 三字段统一：`u_AliveCountPC / u_DeltaTimePC / u_UsePredictedPosPC`。
> **Why**：(1) SPH stable params 高度重叠（h / mass / restDensity / cellSize 等都在 ≥4 shader 复用），独立 UBO 等于 7 次重复上传同样数据；(2) cpp 端只维护一份 `SPHParamsUBO`，每帧调用 `SetData` 一次，所有 dispatch 共享；(3) push constant 三字段覆盖所有 shader 实际需求，某 shader 不读的字段无害（PC 是寄存器写入，读不读取决于 shader main 函数）。
> **How to apply**：跨 ParticleSystemGPU / FluidSystemGPU 不共享 UBO 实例（每系统持自己的 `m_SPHParamsUBO`），但 GLSL 层共享 layout；新增 SPH shader 时复用本 UBO + PC 结构，避免再分裂。Vulkan 路径 GLSL 内用 `#define u_XXX` 映射到 UBO 字段，OpenGL 路径 `uniform u_XXX` 保留（P-14）。

### D-16：主循环显式驱动帧边界 — `GraphicsContext::{Begin,End}RenderFrame`

> **Decision**：`GraphicsContext` 抽象层加 `virtual void BeginRenderFrame() {}` / `virtual void EndRenderFrame() {}` 默认 no-op；`VulkanContext` override：`BeginRenderFrame`=`BeginFrame`（acquire swapchain + Begin cmd），`EndRenderFrame`=`RecordDefaultFramePasses`（清屏/debug/ImGui）+ `EndFrame`（submit + present）。`SwapBuffers` 在 Vulkan path 退化为 no-op，OpenGL 仍 `glfwSwapBuffers`。`Application::Run` 主循环顺序：`BeginRenderFrame → layer->OnUpdate → ImGui Begin/End → EndRenderFrame → Window::OnUpdate`。
> **Why**：Phase 7 已将 `ParticleSystemGPU::UpdateVulkan` / `FluidSystemGPU` 改为录入主帧 cmd（D-3 / ADR-0002），它们在 `OnUpdate` 期间调 `ctx->GetCurrentFrameCommandBuffer()`。Phase 8 接通 EditorLayer 后此调用真实发生，但原 `BeginFrame` 在 `Window::OnUpdate → SwapBuffers` 内部、晚于 `OnUpdate` → `m_FrameInProgress==false` cmd 为空。把帧边界提前到主循环显式驱动，让 dispatch 拿到的 cmd 始终有效。
> **How to apply**：所有需要每帧录主帧 cmd 的系统在 `Layer::OnUpdate` 阶段直接调 `VulkanContext::Get()->GetCurrentFrameCommandBuffer()`；SmokeLayer 也走主循环统一流程，不需要单独调 SwapBuffers。新增 GraphicsContext 实现必须 override 这两个虚函数（OpenGL 留空即可）。同时消化 P-11 接口正向修（IBL 按 API 分派 view vs ID）— SkyboxSystem 加 GetXxxView() 透传 + SceneRenderer GeometryPass 按 RendererAPI::GetAPI() 分派。

---

## 4. 已知陷阱（Pitfalls）

| # | 陷阱 | 触发条件 | 规避 |
|---|------|---------|------|
| P-1 | `prefix_sum` 三 pass 之间 barrier 不能省 | SpatialHashGrid scan 阶段 | 每 pass 间插入 `ResolveBarrierBits(ShaderStorage)`，原 plan 风险点 4 已确认 |
| P-2 | 主帧 stall | 每帧调用 `BeginSingleTimeCommands` | 见 D-3，每帧路径禁用 SingleTime |
| P-3 | ~~IBL 用占位 env atlas~~ — **已消化**（`28fcfc9`）：`VulkanTextureCubemap` 实装后 `VulkanIBLGenerator::Generate` 用真实 cube view + sampler 输入 Irradiance/Prefilter dispatch | — | — |
| P-4 | ~~Editor 主路径在 Vulkan 下显示空场景~~ — **已消化**（`8f16fa0`）：`EditorApp.cpp` 删硬切走 EditorLayer + 主循环 BeginRenderFrame/EndRenderFrame 时序 | — | — |
| P-5 | `Engine/src/` 重新引入 `#include <glad/gl.h>` | 新增 OpenGL-only 代码 | Phase 1 已封堵，新代码必须走 `RendererAPI` 抽象；考虑加 lint hook |
| P-6 | vcpkg manifest 安装失败 — `curl error 35 SSL connect error` | CMake configure 时 vcpkg 从 GitHub 拉源码 | 见 D-8；优先 Vulkan SDK 自带；必要时换 vcpkg baseline 或加 git mirror |
| P-7 | `u8"中文"` 在 C++20 报 C2664 类型不匹配 ImGui `const char*` | ImGui::Text / Begin 等 API 传字面量 | 项目用 `/utf-8` 编译，**普通 `"中文"` 即可**，不要加 `u8` 前缀；`char8_t` 与 `char` 是不同类型 |
| P-8 | Bash 工具 `cd vendor/<submodule>` 后续命令仍在子目录运行 | 子模块内 reset/查 status 后继续工作 | 用 `git -C "<repo-root>" ...` 显式指定主仓库根；或用绝对路径，禁止依赖 cwd 状态 |
| P-9 | 多次 `SpatialHashGrid::BuildVulkan` 同帧调用，pool Reset() 内置会让前次 set 失效 → cmd submit UAF | PCISPH 8 迭代每次内部 Grid.Build / 同帧粒子+流体共享 Grid | pool reset 移出 BuildVulkan；调用方每帧首次显式 `ResetFrameResources()`；pool 容量扩到 64 |
| P-10 | 主帧单 submit 只 signal 1 个 fence | AsyncReadback ring 需独立 fence | 不复用 swapchain inFlightFence；EndFrame 在主 submit 之后追加零 cmd submit 信号化 |
| P-11 | ~~IBL HasIBL 与 GetXxxMapID=0 不一致~~ — **已消化**（`1cc2c6a` 止血 + `28fcfc9` cubemap 接入 + `8f16fa0` view 接口正向修）：完整 Generate 末尾才 `m_IBLReady=true`；SkyboxSystem + IBLGenerator 加 `GetXxxView()` void* 透传，SceneRenderer GeometryPass 按 API 分派 | — | — |
| P-12 | Grass `DrawArraysIndirect` stub — **已止血**（`e7fa266`）：`GrassRenderSystem::Init` Vulkan path 强制 `m_UseIndirectDraw=false` 走 instanced。真实 `vkCmdDrawIndirect` 实装 + `VulkanStorageBuffer::GetRendererID` 唯一化为 Phase 8.2 可选附录 | Vulkan path 接通 Scene 渲染后启用 indirect 路径时触发 | 当前 fallback 已规避；后续实装时需新建 SSBO ID→VkBuffer 映射表 |
| P-13 | `VulkanShader` push constant 反射强制 offset=0 单段（取 `max(size)`） | 未来 shader 使用 `layout(offset=...)` 多段 push constant | 当前所有迁移 shader 都从 0 起始且单段，约束成立；新增多段 PC 时扩展 `m_ReflectedPushConstants` 为按 range 数组 |
| P-14 | 双路径 shader 改写时把 OpenGL `uniform vec3 u_EmitterPos` 改名/改类型 → cpp 端 `m_Shader->SetVec3("u_EmitterPos", ...)` 断裂 | Vulkan 迁移时为对齐宏命名一并改 OpenGL uniform 标识 | OpenGL 路径 `uniform 名 / 类型 / 个数`完全保留，新增 `#ifdef VULKAN` 分支独立写 `push_constant` / `UBO`，main 函数用 `#define` 宏（如 `MAX_PARTICLES` / `EMITTER_POS`）抹平两侧引用 |
| P-15 | 粒子/流体 Vulkan path 独立 `DescriptorPool` 每帧首次未 `Reset()` → set 累积溢出 pool 容量后 alloc 失败 | 每子系统在 cpp 内持有自己的 pool（独立于 `SpatialHashGrid::ResetFrameResources`） | 每帧首次 dispatch 前显式 `pool->Reset()`；本子系统的 pool reset 与 Grid 的 `ResetFrameResources()` 是两件事，不要互相覆盖职责 |
| P-16 | 粒子 SPH 路径下 `sph_density.glsl` 的 binding=8 SurfaceNormals SSBO 在 ParticleSystemGPU 没有专属 buffer | 粒子 SPH WCSPH 与 FluidSystemGPU 共用 shader 但 ParticleSystemGPU 无 `m_SurfaceNormalBuffer` | 当前 Vulkan path 用 alive list 占位以满足 descriptor layout 非空，shader 内 `u_SurfaceTension>0` 控制是否写入（OpenGL 路径同款隐式约束）；启用粒子表面张力前必须为 ParticleSystemGPU 单独分配 `m_SurfaceNormalBuffer` |

---

## 5. 全局约束

- 迁移**不影响 main 分支**：Editor 默认仍走 OpenGL；`Editor.exe --vulkan` 才进入 Vulkan 路径
- shader 双路径并存（`#ifdef VULKAN`），不允许删 OpenGL 分支
- 新 Vulkan 资源类必须经 VMA，禁止裸 `vkAllocateMemory`
- 每个迁移 commit 都应能单独构建通过（`cmake --build build --config RelWithDebInfo --target Editor`）
- 涉及 `.h`/`.cpp` 改动的 commit 在落盘前已被 `.claude/settings.json` 的 PreToolUse hook 自动 clang-format

---

## 6. Next Steps

Phase 8 已完成 cubemap + Editor 主路径切换 + IBL view 分派；Editor.exe --vulkan 现可进入 EditorLayer。下阶段按优先级：

1. **Phase 8.2: `VulkanRendererAPI::DrawIndexed` 真实实装**（最大阻塞 — PBR mesh 在 Vulkan path 仍走 warn-once-fallback 不渲染）
   - 新建 `VulkanVertexArray.h/cpp`：BufferLayout → `VkVertexInputBindingDescription` / `VkVertexInputAttributeDescription`
   - 新增 `VulkanPipelineCache`：按 (shader hash + render pass + vertex layout) 缓存 `VulkanPipeline::CreateGraphics`，懒创建
   - `VulkanRendererAPI::DrawIndexed` 内：取主帧 cmd → 查/建 pipeline → flush descriptor set（含 PBR cache）→ `vkCmdBindPipeline` + `vkCmdBindDescriptorSets` + `vkCmdBindVertexBuffers` + `vkCmdBindIndexBuffer` + `vkCmdDrawIndexed`
   - 实装 `VulkanRendererAPI::BindCubemapView` / `BindTextureView` 写入 `VulkanContext` 当前帧 PBR descriptor cache
   - 验证：`Editor.exe --vulkan` 加载 `assets/scenes/流体测试.scene` → 天空盒 + PBR mesh + IBL 反射可见

2. **运行时验证（依赖 Phase 8.2 完成）**
   - waterfall demo（`assets/scenes/流体测试.scene`）在 `--vulkan` 下视觉与 OpenGL 等价
   - validation layer 0 error
   - 粒子/流体 Update 不触发 VulkanAsyncReadback assert（bug 3 应被 D-16 主循环时序消化）

3. **可选后续：Grass indirect 路径**（P-12 完整修复）
   - `VulkanStorageBuffer::GetRendererID()` 从 `return 0` 改为 atomic counter 分配唯一 ID
   - `VulkanContext` 新增 `unordered_map<uint32_t, VkBuffer>` SSBO 注册表
   - `VulkanRendererAPI::DrawArraysIndirect` 实装 `vkCmdDrawIndirect` + indirect buffer barrier
   - 完成后可移除 `GrassRenderSystem::Init` 的 Vulkan force fallback (`e7fa266`)

---

## 7. 收尾 Checklist（每次 session 结束前）

- [ ] 已完成项的 `- [ ]` 是否已改为 `- [x]`
- [ ] 新出现的非显而易见选择是否已写进 §3 Decision Log
- [ ] 新发现的踩坑是否已写进 §4 Pitfalls
- [ ] §1 "最近一次 commit" 是否已更新
- [ ] 当前 phase 待办若已重排序，§6 Next Steps 是否同步
