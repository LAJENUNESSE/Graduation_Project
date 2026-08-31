# Vulkan 后端迁移 · SPEC

> **作用**：本文件是 Vulkan 迁移分支的**事实源**。新 session 进入此分支时，第一动作是 `read SPEC.md`，无需翻 git log 拼凑进度。
> **分支**：`feature/vulkan-drawindexed`（= main + Phase 8.2 全量实现 + 运行期稳定性修复，2026-08-29）
> **配套路线图**：[../vulkan-migration-roadmap.md](../vulkan-migration-roadmap.md)（阶段定义与验收标准，长期不变）
> **更新规则**：完成一个 phase 子项立即把 `- [ ]` 改成 `- [x]`；做出非显而易见的选择立即记到 "关键决策"。

---

## 1. 当前位置

- **Phase 1 ~ 8.2 全部完成**：DrawIndexed 真实录制、PBR 几何 pass / 天空盒 / IBL 三 compute / 粒子与 SPH 与流体 compute 模拟 / ImGui 视口全部在 Vulkan path 可用；validation 层报错已从首轮 51 条归零。
- **功能完善批次已全部落地（2026-08-30）**：
  - **IBL cube 化**（`5c476e4`）— Irradiance/Prefilter 输出改 6-layer cube-compatible + Prefilter 5 级 mip 链，PBR 环境光照恢复；顺带修复 skybox push constant 不含 u_ViewProjection 导致天空盒不可见（`a22e9dc`）
  - **粒子 billboard 可见**（`f6a26a9`）— dispatcher 支持 SSBO 绑定 + DrawArraysInstanced 真实录制 + 空顶点输入 pipeline；顺带修复 IBL 资源退出泄漏（`eea850b`）
  - **编辑器拾取接通**（`cf2154c`）— ReadPixel 同步回读 + ClearAttachment + RenderEditorPicking 解禁
  - **粒子 counter 语义对齐**（`dc61fd8`/`c4c6065`/`9addf68`，2026-08-30）— counter 变更 GPU 序化（D-20）+ compact 越界 guard + `ENGINE_PARTICLE_COUNTER_DEBUG` 追踪设施，消除 Vulkan overflow 警告循环，等价性遗留第 1 项结案（§6 有实测数据）
  - **GPU 计时 + validation 归零批次**（`8a6283f`/`7ae1b8e`/`c4996bf`/`1e548fd`，2026-08-30）— GPUTimerQuery 抽象化+工厂分派+VulkanGPUTimerQuery（D-21/P-29），SPH 占位 SSBO 兜底；`--vulkan` 性能面板 GPU 计时生效、validation 0 报错，等价性遗留第 2 项与 backlog 第 7 项结案
  - **草地 billboard 解禁**（feature/vulkan-grass 分支，2026-08-30）— dispatcher 通用 UBO 槽 + GrassVSUBO/GrassFSUBO std140 打包上传 + 阴影槽 view 直通 + TerrainPass 保留 mesh 数据更新 + `--scene` 启动参数（`ece1114`）；草可见、风摆与分布正确、validation 0 报错
  - **草地 FS 黑色剪影修复**（2026-08-31，`b9cd8f4`/`e4a3f72`）— 根因是 FSUBO 打包误用 48B DirLight 步长（D-23），改真 std140 32B 步长后草呈带纹理绿色系、validation 0、GL 回归无异常；**功能遗留清零（§6 遗留 0 结案）**
  - 收尾：validation 门控还原（`ENGINE_VULKAN_VALIDATION` 环境变量）、调试打印清理、SPEC 回写（`38884dd`/`46b399e`）、CI 新增 Windows Vulkan 编译 job（`6804ee7`）
- **运行期状态**：粒子场景/默认场景运行 validation 0 报错，多场景切换与长时运行退出零资源泄漏。
- **下一步**：见 [§6 Next Steps](#6-next-steps)。

---

## 2. 已完成 Phase（按时间倒序）

### Phase 8 — Vulkan PBR pass 接通 ✅

- [x] `VulkanTextureCubemap` 实装 — VMA Image (arrayLayers=6 + CUBE_COMPATIBLE) + cube ImageView + Sampler + 6 region staging（`0c6eb28`）
- [x] `VulkanIBLGenerator::Generate` 接入真实 cubemap — 删占位 envAtlas，descriptor binding=1 改 COMBINED_IMAGE_SAMPLER + cube view；IBL GLSL 双路径 `#ifdef VULKAN samplerCube` / OpenGL atlas + imageLoad（`28fcfc9`）
- [x] `EditorApp.cpp` 删 Vulkan→SmokeLayer 硬切，统一走 EditorLayer（`8f16fa0`）
- [x] `GraphicsContext::{Begin,End}RenderFrame` 抽象 + Vulkan override + `Application::Run` 主循环时序（BeginRenderFrame → OnUpdate → ImGui → EndRenderFrame → Window::OnUpdate）（`8f16fa0`）
- [x] `IBLGenerator::Get{Irradiance,Prefilter,BRDFLut}View / GetIBLSampler` 4 个 `void*` 虚函数 + `VulkanIBLGenerator` override + `SkyboxSystem` 透传 + `RenderCommand::Bind{Cubemap,Texture}View`（`8f16fa0`）
- [x] `SceneRenderer::GeometryPass` IBL 按 API 分派 ID vs View；PBR.glsl IBL 采样器 `#ifdef VULKAN` 显式 `layout(set=0, binding=6/7/8)`（`8f16fa0`）

### Phase 8.2 — DrawIndexed 真实实装 ✅（2026-08-23，8 步）

- [x] `VulkanCommandBuffer` 补 graphics 命令封装（`1c6058d`）
- [x] `VulkanFramebuffer` 暴露 attachment `VkImageView` 访问器（`6d97263`）
- [x] 全部 graphics shader 补齐 Vulkan 显式布局分支（`0e223d8`）
- [x] `VulkanSceneState` 场景渲染状态机（模拟 GL 即时模式"当前 shader + 16 纹理槽"）并接线 Bind 注册点（`5dc6559`）
- [x] `VulkanGraphicsPipelineBuilder` + `VulkanPipelineCache`（key = shader + renderpass + 顶点布局 hash + 光栅状态位，`b5bcda9`；骨架 `2d83029`）
- [x] `VulkanSceneDrawDispatcher`：DrawIndexed/DrawArrays 真实录制（std140 UBO 打包 + per-draw push constant + per-frame descriptor pool + pipeline cache 查询，`2722c7c`）
- [x] `VulkanFramebuffer` 录制场景 renderpass 并接线阴影/后处理绑定（`62feb08`）
- [x] ImGui 视口接入 Vulkan 离屏纹理；空 descriptor 槽用 1×1 白色 2D/CUBE 占位纹理兜底防 device lost（`d0c783e`）

**运行期稳定性修复（2026-08-23 ~ 08-29，validation 51 → 0）**
- `e1b645d` 按 validation 报告修复五类非法状态（CSM 槽映射、samplerCube 绑 2D 占位、depth layout VUID-09600、shadow map layout UNDEFINED、未消费 vertex attribute + SSAO staging 越界）
- `c0c1472` UBO descriptor offset 按 minUniformBufferOffsetAlignment(256) 对齐（真实 mesh device lost 根因）
- `e7c9e54` swapchain 信号量改按图像绑定（VUID-00067 复用校验）
- `83e867b` `VulkanDeletionQueue` 按帧槽位延迟删除（场景切换帧内析构资源崩溃）
- `02bc735` 场景 FBO 布局与材质 UBO 修复
- `e6f0ed3` 全局 UBO 覆盖导致场景空白
- `f2904a6` / `d96a823` / `71d4110` / `d12a707` 延迟销毁场景缓冲、IBL、管线、描述符；粒子计算时序与描述符复用；隔离未完成特效资源与查询池销毁
- `f0e0822` 空 Pass 清屏（loadOp=CLEAR）清掉 GeometryPass 输出导致视口黑屏 → 改 LOAD
- `43ecb11` 退出时先释放 ImGui 描述符再关闭后端（退出 c0000005，WinDbg minidump 定位）
- `38884dd` validation 还原按构建门控（`ENGINE_VULKAN_VALIDATION` 环境变量可强制开启）+ 清理黑屏调试打印

### 功能完善批次（2026-08-30，三大缺口全部闭合）✅

- [x] **IBL cube 化**（`5c476e4`）：Irradiance/Prefilter 输出从 2D 横向 atlas 改 6-layer cube-compatible image（CUBE view 全 mip 供 PBR 采样 + 每 mip 一个 2D_ARRAY view 供 compute 写）；Prefilter 生成 5 级 mip 链（roughness = max(mip/4, 0.05)，与 GL 一致）；`m_IBLReady` 恢复 true，PBR 环境光照生效。shader 卷积主体抽成共享函数，GL 分支零变化
- [x] **skybox 不可见修复**（`a22e9dc`）：Skybox.glsl 的 u_ViewProjection 走独立 mat4 push constant，dispatcher PC 打包只填 u_Transform → 零矩阵顶点退化。PC 打包改 128B 通用块按 uniform 名匹配
- [x] **descriptor pool 补 STORAGE_BUFFER**（`f6a26a9` 一部分）：粒子 SSBO set alloc 直接失败静默丢 draw
- [x] **粒子 billboard 可见**（`f6a26a9`）：SceneState SSBO 槽 + `StorageBuffer::Bind` 记录、dispatcher 支持 STORAGE_BUFFER descriptor 与空顶点输入、PC 打包覆盖 {u_View,u_Projection} 布局、`DrawArraysInstanced` 真实录制（vkCmdDrawInstanced）、粒子系统 Vulkan 强制 direct draw
- [x] **IBL 资源退出泄漏修复**（`eea850b`）：`~VulkanIBLGenerator` 从 default 改调 Shutdown（SkyboxSystem 从不调 Shutdown/Clear，3 image + 9 view + compute 管线/pool/sampler 从不销毁）
- [x] **编辑器拾取接通**（`cf2154c`）：`ReadPixel` 实装（SingleTime 同步回读 + SHADER_READ_ONLY↔TRANSFER_SRC 布局往返 + 4B staging 复用）、color attachment 补 TRANSFER_SRC usage、`ClearAttachment` 实装、`RenderEditorPicking` 解禁（pipeline key 含 RenderPass，原"RED_INTEGER 不兼容"注释已过时）
- [x] **CI**（`6804ee7`）：build.yml 新增 build-windows-vulkan job（runner 装 LunarG SDK 静默安装 → `cmake --preset vs2022-vulkan`）；vs2022-vulkan preset 删除硬编码本机 SDK 路径改用系统环境变量

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

### D-17：上层零改动 — GL 即时模式"快照→打包→录制"模拟层（Phase 8.2 架构核心）

> **Decision**：不重写 SceneRenderer 为 Vulkan 命令式，而是让 `VulkanSceneState`（模拟 GL 全局状态机：当前 shader + 16 纹理槽 + 状态开关）在 `Bind` 注册点累积状态，`VulkanSceneDrawDispatcher` 在 DrawIndexed/DrawArrays 时刻"快照 → std140 UBO 打包 → push constant → 反射式 descriptor 写入 → 录制 vkCmdDrawIndexed"。录制失败的 draw 退化为内嵌 SPIR-V 的 DebugTriangle 管线画到 swapchain（不再触发即静默丢弃）。
> **Why**：OpenGL 调用序列是全引擎的公共语义（SceneRenderer / 各 System 全部按它编写），为 Vulkan 单独改写上层会双倍维护且破坏"main 零回归"约束；模拟层让 37 个 shader 与上层代码单源复用。
> **How to apply**：新增场景功能时**按 OpenGL 语义写上层代码**（Bind shader/纹理 → SetUniform → Draw），由 dispatcher 自动打包；不要在 Vulkan path 直接录 cmd。遇到 dispatcher 不支持的模式（如 instanced、SSBO）在 dispatcher 内扩展，而不是绕过它。

### D-18：运行期资源释放走 `VulkanDeletionQueue` 按帧槽位延迟删除

> **Decision**：`VulkanDeletionQueue` 2 slot 与 MAX_FRAMES_IN_FLIGHT 对齐；运行期需要销毁的资源（FBO、纹理、管线、描述符、场景缓冲、IBL）压入队列，BeginFrame flush 当前槽（该槽资源在 2 帧前的 submit 已完成）。退出路径仍走显式 Destroy。
> **Why**：场景切换/资源重载发生在录制窗口内（上一帧可能仍在 GPU 执行），同步析构 = cmd 引用已销毁资源 → invalid state + device lost（83e867b）。
> **How to apply**：任何运行期 Vulkan 资源析构一律经 `VulkanContext::DeferDestroy`（或等价队列），禁止在录制窗口内直接 `vkDestroy*`；新资源类型接入时提供对应的销毁闭包。

### D-19：validation 按构建门控 + 环境变量强制开关

> **Decision**：`VulkanContext::s_EnableValidation` 为运行时静态变量，`ResolveValidationEnabled()` 规则：`ENGINE_VULKAN_VALIDATION` 环境变量（=1 开 / =0 关）优先，否则 Debug 开、Release 关。
> **Why**：phase-8.2 排查 device lost 期间曾临时无条件强制开启（TODO 挂在头文件）；device lost 六根因修复且 validation 归零后应还原，但完全移除会丧失排查能力，环境变量是折中。
> **How to apply**：日常运行不设变量；排查时 `ENGINE_VULKAN_VALIDATION=1 Editor.exe --vulkan`。

### D-20：粒子 counter 等跨帧累计 buffer 的变更一律 GPU 序（`c4c6065`）

> **Decision**：粒子 counter buffer 的每帧变更（aliveCount=0 清零、emitCount 写入、compact 前 16B 清零、overflow sanitize 回写）全部录制进帧 cmd：`VulkanCommandBuffer::FillBuffer`（vkCmdFillBuffer）/ `UpdateBuffer`（vkCmdUpdateBuffer，≤64KB）+ BufferUpdate barrier（TRANSFER 写 → COMPUTE 读）；回读 copy 前补 COMPUTE/TRANSFER 写 → TRANSFER 读 barrier。禁对跨帧累计 buffer 做 host 立即写（`SetData` 的 host-visible 直写路径）。
> **Why**：host 立即写与 2 帧在飞的 GPU dispatch 无顺序保障，落点漂进上一帧 dispatch 序列——实测 `ENGINE_PARTICLE_COUNTER_DEBUG` 显示 sanitize 回写(dead=10000)落地后下一帧 compact 又累加 → 回读恒 20000/30000 → 每帧 overflow 警告 + clamp 循环（等价性遗留第 1 项）。GL 免疫是因单命令流内 CPU 写天然有序；GPU 序化后 Vulkan 与 GL 语义对齐。
> **How to apply**：新增"CPU 每帧更新 + GPU 跨帧累计"的 buffer（各系统 counter/累计器）一律走 FillBuffer/UpdateBuffer + barrier 模板；仅本帧 CPU 写、GPU 读的一次性参数（UBO）可保留 host 写（参数逐帧不变时实际无害，见 P-28 备注）。跨帧 copy 与下一帧 FillBuffer 的 WAR 依赖同队列按序假设（与 D-11 空 submit fence 同级）。

### D-21：Vulkan GPU timestamp 计时——宿主重置 + 帧内游标 + 多 pair begin 锚（`1e548fd`）

> **Decision**：`VulkanGPUTimerQuery`（2 帧槽 × 每槽 4 对 × 2 query）——(1) 重置走**宿主端 `vkResetQueryPool`**（hostQueryReset 特性，`VulkanContext` 建设备时经 `VkPhysicalDeviceVulkan12Features` pNext 启用），不用 cmd 重置；(2) 同帧多对 Begin/End（编辑器主渲染+拾取重录 GeometryPass）经帧内游标占用**不同 query 对**，`VulkanContext::GetFrameCounter()` 单调帧号驱动游标复位；(3) Begin/End 两侧都用 `BOTTOM_OF_PIPE`。
> **Why**：实测（RTX 3050 Ti，validation+原始值探针）三条驱动事实：cmd `vkCmdResetQueryPool` 录进 render pass 被 validation 拒绝丢弃（计时器调用点嵌在 HDR FBO 的 render pass 内——`VulkanFramebuffer::Bind` 即 BeginRenderPass）；"query uses 之间必须 reset" 禁止同帧两对写同一 query；**render pass 内的 timestamp 写入（TOP/BOTTOM 皆然）被驱动合并到同一时刻，pair 内 delta 恒 0**。多 pair 时相邻 pair 的 begin 锚差值（= 下一 pass 起点 − 本 pass 起点）恰为主 pass GPU 时长（拾取 pass 起点≈主 pass 结束）；单 pair（pass 外）保持 end−begin。
> **How to apply**：新增计时器直接经 `PerformanceMonitor::Get().GetXxxGPUTimer()` 访问器使用（工厂按 API 分派，`8a6283f`）；Begin/End 调用点无需关心是否在 render pass 内。局限：单 pair 且嵌在 pass 内的计时在 Play 模式（无拾取重录）退化为 0——需精确时把测量点放 pass 外，或启用 synchronization2 的 `vkCmdWriteTimestamp2`。

### D-23：std140 布局镜像必须按 GLSL 规则逐槽推导，不能按 C++ 直觉补 padding —— 草地 FS 黑剪影根因（`e4a3f72`）

> **Decision**：cpp 侧 UBO 镜像结构体与 GLSL std140 块对齐时，`struct{vec3,vec3,float}` 这类"最后一个 float 恰好填满前一 vec3 的 16B 槽尾"的组合，**float 必须紧贴前一 vec3（offset 28）**，struct 大小 32B、数组步长 32B——不是每成员都补到 16B 边界的 48B。判据：std140 只把 `vec3/vec4/mat/数组/结构体数组首地址` 对齐到 16B，**标量成员从不强制 16B 对齐**。修复后 GrassFSUBO 块 = 96B（数据 84B 按块对齐 round）。
> **Why**：GrassFSDirLightStd140 误按"vec3 各占 16B 槽"把 Intensity 放 @32/步长 48B，GPU 按 std140 解释时尾部标量全部错位（NumDirLights 读 @64 实际写在 @96、AmbientStrength 读 @80 实际写在 @112）→ lighting=0 → 黑剪影。此前 SPEC 记录的"逐字节一致"结论是 **cpp 结构体自证 static_assert** 得出的，镜像本身写错时 assert 只会确认错误。黑剪影的迷惑性在于：validation 0（布局非法才会报）、buffer 内容正确（按 CPU 语义回读 0.3 在）、VSUBO 正常（无 struct 数组，两种步长下偏移恰好相同）、GL 正常（散装 uniform 无内存布局）。
> **How to apply**：写 UBO 镜像结构体时，offset 从 GLSL 规则推导后**用 RenderDoc 反射的块大小（byteSize）交叉验证**（本次 FSUBO 反射 84B vs 镜像 128B，一眼即穿）；静态断言只能防"两边都错成一致"之外的漂移，不能证明语义正确。诊断路径沉淀：`[DbgGenericUBO]` 按 binding 打 hit/miss（`b9cd8f4`）+ `UploadToAllocation` invalidate 回读 + RenderDoc debug_pixel/`export-buffer` 三件套可在一轮内闭环"CPU 打包→内存→descriptor→FS 读取"全链（P-30 所述"注入抓帧不录 command"已不复现，2026-08-31 实测）。

### D-22：草地 billboard 接通 — 通用 UBO 槽 + per-frame 双份 UBO

> **Decision**：场景 shader 的非 Global/Lights/Material 命名 set0 UBO 走"通用 UBO 槽"三段式——`VulkanSceneState::BindUniformSlot`（槽 0~7）+ `VulkanUniformBuffer::Bind(binding)` 录入（抽象层 `UniformBuffer::Bind` 纯虚，GL 实现为幂等 `glBindBufferBase`）+ dispatcher UBO 分支 set0 回退（Invalid 跳过，同 SSBO 分支语义）；descriptor pool UNIFORM_BUFFER 按 P-27 扩为 5/draw。草地侧 GrassVSUBO(196B std140)/GrassFSUBO(128B，DirLight std140 步长 48B) 由 GrassRenderSystem 打包，**per-frame-in-flight 双份**（`GrassInstance::VSUbo[2]/FSUbo[2]`，按 `GetCurrentFrameIndex()` 索引），与 dispatcher FrameResources 同惯例；阴影槽走 `BindTextureView(1, GetShadowDepthView(CSMActive?0:CSM_MAX_CASCADES))`（`shadowDepthView` 参数经 SceneRenderer 传入，void* 透传）。
> **Why**：grass_billboard.glsl VULKAN 分支的 GrassVSUBO/GrassFSUBO 无 cpp 侧创建/上传代码（GL 用散装 uniform），descriptor 静默留空（P-19）；草 UBO 含 per-frame（Time/相机/灯光）与 per-entity（Transform/EntityID）两类数据，单份 UBO 跨帧复用存在"帧 N+1 CPU 写覆盖帧 N 未执行 draw 读取"的竞争，per-frame 双份借 BeginFrame fence 等待严格消除。
> **How to apply**：新增场景 shader 自定义 set0 UBO 时按此模板——cpp 端 `UniformBuffer::Create(256 对齐, binding)`（GL 路径不 alloc）→ 每帧 `SetData` 打包 std140 → `Bind(binding)` 录槽 → dispatcher 自动消费；per-frame 变更数据用双份按帧索引。顺带注意：**TerrainPass 的 `UpdateTerrainMeshes` 是纯 CPU 侧**（高度图→TerrainMeshData），草地 placement 依赖它，地形绘制解禁前 Vulkan 分支必须保留数据更新（`55537a5`）；`u_GrassTexture` 的 sampler 名→槽映射为 unit 2（`4cf0853`）。

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
| P-17 | Debug build 链 spirv-cross-core.lib 报 `LNK2038 _ITERATOR_DEBUG_LEVEL 0 vs 2` / `RuntimeLibrary MD vs MDd` | Vulkan SDK 同时提供 Release (`spirv-cross-core.lib` MD) 和 Debug (`spirv-cross-cored.lib` MDd) 两套；CMake 若只 find Release 名，Debug build 链 Release lib → MSVC CRT 不匹配拒链 | `find_library` 分别 Release/Debug 双 find，`target_link_libraries` 用 `$<$<CONFIG:Debug>:...>` 生成器表达式按 config 切。新增 Vulkan SDK 依赖时（glslang/SPIRV-Tools 等）同样要查双版本（D-8 衍生约束）|
| P-18 | UBO dynamic offset 未按 `minUniformBufferOffsetAlignment`(256) 对齐 → 真实 mesh 触发 VK_ERROR_DEVICE_LOST | dispatcher 每段 UBO 起始 offset 由 std140 大小累加而来（如 768B 段后 768+768=1536 非对齐） | 打包大小一律 round up 到 256 对齐（`c0c1472`）；新增 UBO 段类型时复用 dispatcher 现有对齐常量 |
| P-19 | descriptor 槽留空（如 SSAO/流体 pass 未绑纹理）→ 执行 device lost | pass 被跳过 / shader 声明了绑定但上层未 Bind | 1×1 白色 2D + CUBE 占位纹理兜底所有未绑定槽（`d0c783e`）；新增纹理槽消费时确认 shader 声明与上层 Bind 对齐 |
| P-20 | swapchain 信号量跨帧复用 → VUID-00067；acquire 用 semaphore 同样有复用歧义 | renderFinished 与 swapchain image 数不一致时 | renderFinished 按 image 一比一创建；acquire 改用独立 fence host 等待（`e7c9e54`） |
| P-21 | HDR FBO 的空 pass（无 draw）`loadOp=CLEAR` 会清掉先前 GeometryPass 写入的颜色 → 视口黑屏 | PostProcessing 前的占位 renderpass 录制 | 场景 renderpass 按帧内容决定 loadOp：已有几何输出的 pass 用 LOAD（`f0e0822`） |
| P-22 | 录制窗口内同步析构资源（场景切换/重载）→ cmd 引用已销毁资源 | 上层在 OnUpdate 中释放 Ref<FBO>/Texture/Buffer | 运行期析构一律走 D-18 deletion queue |
| P-23 | 退出崩溃 c0000005：ImGuiLayer OnDetach 先 Shutdown 后端，Layer 析构再 `RemoveImGuiTexture` 解引用已释放描述符 | 编辑器退出时视口纹理仍注册在 ImGuiLayer | 退出时先 `FlushDeferredDestructions` + `ClearImGuiTextures` 再关后端（`43ecb11`）；新增 ImGui 相关释放路径时注意此顺序 |
| P-24 | 场景 shader 的 push constant 布局因 shader 而异：PBR=`{Transform, NormalMatrix}` 112B、Skybox=单 mat4、粒子=`{u_View, u_Projection}` 128B | 新增 PC 场景 shader 或改现有 PC 布局 | dispatcher PC 打包按 uniform 名匹配（Slot0: u_Transform→u_ViewProjection→u_View；Slot1: NormalMatrix 或 u_Projection，`f6a26a9`）；新布局需扩名匹配表，禁止假设固定结构 |
| P-25 | `VulkanIBLGenerator` 析构 default + SkyboxSystem 无析构 → IBL 全部 GPU 资源从不销毁（vkDestroyDevice 时 validation 报泄漏） | 任何"靠 Ref 释放即清理"的资源类 | 资源类析构必须显式调 Shutdown/Clear（`eea850b`）；新增资源类时析构函数调清理，并确认清理对 context 存活有防御 |
| P-26 | ReadPixel 回读要求 image usage 含 `TRANSFER_SRC_BIT`，否则 copy 非法 | FBO attachment 创建时未预埋回读用途 | `ColorUsageFlags()` 已加 TRANSFER_SRC（`cf2154c`）；新增回读类功能先查 usage 位 |
| P-27 | descriptor pool 缺新 descriptor 类型时 alloc **静默失败** → draw 被丢弃（仅 validation 报 pool 类型缺失） | dispatcher 支持 SSBO 等新绑定类型但 pool size 未同步 | per-frame pool sizes 与 dispatcher 支持的类型同步维护（`f6a26a9`）；新增 descriptor 类型时先补 pool |
| P-28 | host 立即写跨帧累计 buffer 与 2 帧在飞 GPU dispatch 无顺序保障 → 清零落点漂进上一帧 dispatch 序列，compact/simulate 在"回写值"上二次累加（counter 双倍累积，实测 readback dead=20000/30000 vs probe=10000） | SPH 场景每帧 compact 全量重建 counter + GPU 滞后 ≥1 帧（该场景 Vulkan ~28fps vs GL ~77fps，滞后常态） | 跨帧累计 buffer 变更一律 GPU 序（D-20：FillBuffer/UpdateBuffer + BufferUpdate barrier）；诊断用 `ENGINE_PARTICLE_COUNTER_DEBUG=1` 逐帧对比 probe（GPU 内存直读）与 readback（异步回读），两者系统性差值即乱序证据 |
| P-29 | GPU 计时器的 cmd 重置/timestamp 写入与 render pass 的三条硬约束：cmd `vkCmdResetQueryPool` 进 render pass 被 validation 拒绝丢弃；同帧多对 Begin/End 写同一 query 触发 "reset between uses"；pass 内 timestamp 写入被驱动合并（pair 内 delta 恒 0） | 计时器调用点嵌在 HDR/shadow render pass 内（`VulkanFramebuffer::Bind` 即 BeginRenderPass）+ 编辑器拾取每帧重录 GeometryPass | 宿主端 `vkResetQueryPool`（需启用 hostQueryReset——**注意 SDK 1.4 头的 sType 拼写是 `...VULKAN_1_2_FEATURES`（1_2 而非 12），基础 VkPhysicalDeviceFeatures 里没有该字段**）+ 帧内游标分 query 对 + 多 pair begin 锚差值（D-21） |
| P-30 | ~~RenderDoc 注入运行时**主场景 HDR pass 整体不录 command**~~ — **已失效**（2026-08-31 实测）：注入 `renderdoc-cli capture`（正确语法 `capture EXE -w DIR -a ARGS -d N -o OUT.rdc`）抓到完整帧（54 events，HDR/picking/shadow/ImGui pass 与 dispatcher draws 齐全），debug_pixel/`export-buffer` 一步定位草地黑剪影根因 | ~~同左~~ | 视觉类问题优先走抓帧 + debug_pixel（D-23 三件套），仅当抓帧异常时退回引擎侧诊断日志路线 |
| P-31 | cpp UBO 镜像结构体按"每成员补到 16B 边界"的 C++ 直觉写 → 与 GLSL std140 真实布局错位，尾部标量全读 0；validation 不报、static_assert 自证错误、内容回读"正确" | 镜像含 `struct{vec3,vec3,float}` 或其他"标量可塞进前一 vec3 槽尾"的组合（如 GrassFSUBO DirLight） | 按 std140 规则逐槽推导（D-23）：只有 vec3/vec4/mat/数组/结构体首址对齐 16B，**标量不对齐**；用 RenderDoc 反射 byteSize 交叉验证镜像尺寸 |

---

## 5. 全局约束

- 迁移**不影响 main 分支**：Editor 默认仍走 OpenGL；`Editor.exe --vulkan` 才进入 Vulkan 路径
- shader 双路径并存（`#ifdef VULKAN`），不允许删 OpenGL 分支
- 新 Vulkan 资源类必须经 VMA，禁止裸 `vkAllocateMemory`
- 每个迁移 commit 都应能单独构建通过（`cmake --build build --config RelWithDebInfo --target Editor`）
- 涉及 `.h`/`.cpp` 改动的 commit 在落盘前已被 `.claude/settings.json` 的 PreToolUse hook 自动 clang-format

---

## 6. Next Steps

三大核心缺口（IBL、粒子 billboard、拾取）已全部闭合，Vulkan path 达到"能看能用"基本面。后续按需启动：

**功能遗留**：0 项。~~草地 billboard FS 黑色剪影~~ — **已解决**（2026-08-31，`b9cd8f4`/`e4a3f72`）：根因是 FSUBO 打包误用 48B DirLight 步长，GPU 按 std140（32B 步长）解释时尾部标量错位读 0 → lighting=0 → 黑剪影（D-23/P-31）。定位链：纹理/v_Color 二分实验排除 → descriptor 指针比对 + invalidate 回读证明 CPU→内存→descriptor 全链正确 → RenderDoc debug_pixel 实证 FS 端 `u_AmbientStrength=0.0` 且反射块大小 84B 暴露布局错位。修复后草呈带纹理绿色系（RenderDoc 像素统计 8291 草叶像素 100% G>R、0% 纯黑）、validation 0、GL 回归无异常。诊断设施沉淀：`[DbgGenericUBO]` 按 binding 打 hit/miss、`[Grass][Vulkan] UBO pack` 前 5 帧。注：本场景方向光收集 numDir=0（层级有方向光实体但 CollectLights 未收到，GL/Vulkan 同现象）为独立既存问题，ambient=0.3 足以显示，已单独留意

**等价性遗留**
1. ~~**粒子计数语义差异**~~ — **已解决**（`dc61fd8`/`c4c6065`/`9addf68`，2026-08-30）：根因是 counter host 立即写与 2 帧在飞 dispatch 无序（P-28），4 处变更 GPU 序化后对齐 GL（D-20）。修复前后实测（`ENGINE_PARTICLE_COUNTER_DEBUG=1`，粒子测试.scene，EmitRate=300）：修复前 Vulkan 回读 dead=20000/30000（probe=10000）、每帧 overflow 警告 + corrected=1；修复后 2610 帧 0 警告 0 corrected，probe==readback，dead+alive≈10146 与 GL 基线（9858/288，77fps）同特征同量级（Vulkan 74fps）。注：GL/Vulkan 的 alive 计数本身含 compact+simulate 同帧双计（≈2×真实存活，低于 max 不报错），两后端一致，属既存语义非缺陷。等价性验证设施说明：`ENGINE_PARTICLE_EQUIV_SMOKE` 为 GL↔CUDA 单进程对比，无法覆盖 Vulkan（单进程单 RHI，GL compute 封装在 Vulkan 上是 stub；且 counter 语义分歧不体现在粒子数据快照里）——Vulkan 侧以 counter 追踪 + 双后端实测协议替代（见 D-20/P-28），真跨 RHI 对比需离线进程编排，暂不做
2. ~~**SPH/流体 compute 占位 buffer validation**~~ — **已解决**（`c4996bf`，2026-08-30）：sph_force.glsl 声明 binding 3/10/11，耦合关闭时从未写入触发 3 条 "never been updated"。粒子侧无条件懒创建占位（binding 3 复用 InitRigidBodyBuffer、10/11 新增 16B GPUOnly 占位，dispatchSPH 增加 bindMeshSDF 参数）；流体侧同款无条件 InitRigidBodyBuffer/InitMeshSDFBuffer 加固（流体 pass 当前 Vulkan 跳过不触发 validation，属提前加固，接通后需复验）。实测 `ENGINE_VULKAN_VALIDATION=1` 运行 0 报错
3. **阴影健壮性**：CSM 模式下主 shadow map FBO 从未执行 renderpass（`SceneRenderer.cpp:337-343` 注释），地形也不写入 shadow map——注：GPU 计时器（D-21）实测 ShadowPass GPU avg 0.000ms 即此空转的真实反映

**功能 backlog（按价值排序）**
4. ~~**草地 billboard 接通**~~ — **已完成**（feature/vulkan-grass，见 D-22 与 §1）；P-12 indirect 仍未解
5. **Bloom**（`SceneRenderer.cpp` 后处理段，依赖 GL ID 传递改 view 直通，参照 tone mapping 先例）
6. **SSAO**（`VulkanTexture.cpp` RGB_Float 上传 + callerFBO 语义）
7. ~~**VulkanGPUTimerQuery**~~ — **已完成**（`8a6283f`/`7ae1b8e`/`1e548fd`/`c4996bf`，2026-08-30）：GPUTimerQuery 抽象化 + 工厂分派 + VulkanGPUTimerQuery（VkQueryPool TIMESTAMP，D-21/P-29）；实测 --vulkan 性能面板 Scene GPU avg 0.047ms、Particle GPU avg 0.234ms、Shadow avg 0（shadow pass 空转的真实反映）、validation 0 报错；GL 复跑与基线一致零回归。局限：单 pair 且 Begin/End 嵌在 render pass 内的计时在 Play 模式（无拾取重录）退化，见 D-21
8. **screen-space 流体链**（深度/厚度/composite，依赖 2 的 counter 对齐更好）
9. **地形 pass**（Terrain UBO/descriptor 接入 dispatcher）
10. **物理调试线框画进视口**（debug 兜底队列从 swapchain 重定向到场景 renderpass）
11. **其余**：MSAA、VulkanSwapchain 从 VulkanContext 拆出（stub）、Blend/Scissor/ColorMask 状态消费、CUDA-Vulkan 互操作（`docs/cuda-reintroduction-report.md` 明确当前阶段不恢复）

**工程化**
12. 分支 `feature/vulkan-drawindexed` 合回 main 的时机由用户决定（CI 已覆盖 Vulkan 编译）

---

## 7. 收尾 Checklist（每次 session 结束前）

- [ ] 已完成项的 `- [ ]` 是否已改为 `- [x]`
- [ ] 新出现的非显而易见选择是否已写进 §3 Decision Log
- [ ] 新发现的踩坑是否已写进 §4 Pitfalls
- [ ] §1 "最近一次 commit" 是否已更新
- [ ] 当前 phase 待办若已重排序，§6 Next Steps 是否同步
