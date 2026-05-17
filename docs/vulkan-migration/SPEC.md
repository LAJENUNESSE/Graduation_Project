# Vulkan 后端迁移 · SPEC

> **作用**：本文件是 Vulkan 迁移分支的**事实源**。新 session 进入此分支时，第一动作是 `read SPEC.md`，无需翻 git log 拼凑进度。
> **分支**：`feature/vulkan-backend`（自 `main` 分叉 34 个 commit）
> **配套路线图**：[../vulkan-migration-roadmap.md](../vulkan-migration-roadmap.md)（阶段定义与验收标准，长期不变）
> **更新规则**：完成一个 phase 子项立即把 `- [ ]` 改成 `- [x]`；做出非显而易见的选择立即记到 "关键决策"。

---

## 1. 当前位置

- **进行中阶段**：Phase 7（Compute 迁移）
- **最近一次 commit**：`42eb259 phase7: FluidSystemGPU fluid_emit/simulate Vulkan 化 + MeshSDFMeta 异步回读预埋`
- **下一步**：见 [§6 Next Steps](#6-next-steps)（仅剩 Commit C — SPH 整体迁移）

---

## 2. 已完成 Phase（按时间倒序）

### Phase 7 — Compute 迁移 ⬅️ 进行中

**基础设施**（已就绪）
- [x] `VulkanContext` 增加 compute queue family 查询（`16a3db0`）
- [x] `VulkanShader` 暴露 SPIR-V + `VkShaderModule` 懒创建缓存（`29ed242`）
- [x] `VulkanShader` 集成 spirv-cross 反射 descriptor binding + push constant（`18f4277`）
- [x] `VulkanDescriptor` 三层抽象（`SetLayout` / `Pool` / `Writer`）（`ff45e44`）
- [x] `VulkanPipeline::CreateCompute` 工厂（`fed450e`）
- [x] `VulkanCommandBuffer` compute 命令封装（`f547bd0`）
- [x] `VulkanBarrierUtil` — `BarrierBit::{ShaderStorage,Command,BufferUpdate,All}` → `VkPipelineStage`/`VkAccess` 映射（`6f577a5`）
- [x] `VulkanStorageBuffer::ExternalMemoryHint` 占位（CUDA 互操作，Phase 7.5 实装）（`2625fa4`）
- [x] `VulkanContext` 拆 `BeginFrame()` / `EndFrame()` / `GetCurrentFrameCommandBuffer()` + readback fence 信号化 hook（`777de43`）

**子系统迁移**
- [x] `IBLGenerator` — BRDF LUT / Irradiance / Prefilter 三 compute dispatch（`6ca3cb3`）
- [x] `GrassRenderSystem` — placement + render_args 两 pass，DrawArraysIndirect 链路（`97750ae`）
- [x] `SpatialHashGrid` — hash / prefix_sum (三 pass) / scatter（`93b2e2d`）
- [x] `AsyncReadback` ring buffer — VulkanAsyncReadback 3 槽 + 独立 fence + EndFrame 零 cmd submit 信号化（`ad31c42`）
- [x] `SpatialHashGrid::BuildVulkan(cmd, ...)` 接受外部 cmd buffer + ResetFrameResources，每帧调用方录主帧 cmd（`c7b2c92`）
- [x] `ParticleSystemGPU` 非 SPH 路径 — emit / simulate / render_args 3 dispatch + 4 shader（`399bd2c`）
- [x] `FluidSystemGPU` emit/simulate — 2 dispatch + 2 shader + MeshSDFMeta 异步回读预埋（`42eb259`）
- [ ] SPH 整体迁移（Commit C）— sph_density / sph_force / sph_pcisph_*(5) 7 shader + FluidSystemGPU::UpdateSPHVulkan PCISPH 8 迭代 + 回头补 ParticleSystemGPU SPH 分支
- [ ] `VulkanTextureCubemap` — 解锁 IBL 真实 skybox 像素，目前用占位 env atlas

### Phase 6 — ImGui 集成（部分完成）

- [x] `imgui_impl_vulkan` 初始化 + RenderPass + descriptor pool（`5f0fe51`）
- [x] 多视口（Vulkan only）
- [x] `VulkanSmokeLayer` 增加 ImGui 验证窗口（`d807afe`）
- [ ] `VulkanContext::RenderImGui()` 实际渲染 ImGui draw data（当前 stub）—— 阻塞 Editor 主路径

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
> **How to apply**：**每帧执行的 dispatch**（粒子/流体 step）禁止再走 SingleTime，必须录制到主帧 command buffer。Phase 7.5 把现有调用点统一改造。

### D-4：`grassCount` 同步阻塞回读

> **Decision**：`vmaMapMemory` 直接读 host-visible buffer，跳过 OpenGL 的 fence 轮询。
> **Why**：RebuildGrass 低频；fence ring 复杂度先省掉。
> **How to apply**：粒子/流体的 CPU↔GPU 回读迁移时**不要**复制此模式，必须实现 `AsyncReadback` ring buffer（多帧 in-flight + fence 轮询）。

### D-5：`SpatialHashGrid::SetExternalBuffers(...)` 显式注入

> **Decision**：Vulkan 路径要求调用方（`ParticleSystemGPU` / `FluidSystemGPU`）显式 setter 注入 particlePool / aliveList / pcisphPool。
> **Why**：Vulkan 没有 OpenGL 的全局 SSBO binding，descriptor set 必须知道具体 buffer。
> **How to apply**：粒子/流体迁移时**先**补 setter 调用，再迁移自身 compute，否则 SpatialHashGrid 在 Vulkan 路径下拿不到外部 buffer。

### D-6：`ExternalMemoryHint` 占位推迟到 Phase 7.5

> **Decision**：`VulkanStorageBuffer::ExternalMemoryHint::CudaInterop` 当前断言未实现。
> **Why**：CUDA-Vulkan 互操作需要 `VK_EXTERNAL_MEMORY_HANDLE_TYPE_*` + Win32/Fd handle 导出导入链路，Phase 7 主线先不引入。
> **How to apply**：上层 API 透传该枚举即可；真正启用时机由 CUDA sidecar 迁移驱动。

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

---

## 4. 已知陷阱（Pitfalls）

| # | 陷阱 | 触发条件 | 规避 |
|---|------|---------|------|
| P-1 | `prefix_sum` 三 pass 之间 barrier 不能省 | SpatialHashGrid scan 阶段 | 每 pass 间插入 `ResolveBarrierBits(ShaderStorage)`，原 plan 风险点 4 已确认 |
| P-2 | 主帧 stall | 每帧调用 `BeginSingleTimeCommands` | 见 D-3，每帧路径禁用 SingleTime |
| P-3 | IBL 用占位 env atlas | `VulkanTextureCubemap` 未实装 | 仅 BRDF LUT 可信，Irradiance/Prefilter 输出无实际意义直到 Cubemap 完成 |
| P-4 | Editor 主路径在 Vulkan 下显示空场景 | `--vulkan` 走 `VulkanSmokeLayer` | 不要拿"Vulkan 下 Editor 没渲染"当 bug，PBR 接入是 Phase 8 任务 |
| P-5 | `Engine/src/` 重新引入 `#include <glad/gl.h>` | 新增 OpenGL-only 代码 | Phase 1 已封堵，新代码必须走 `RendererAPI` 抽象；考虑加 lint hook |
| P-6 | vcpkg manifest 安装失败 — `curl error 35 SSL connect error` | CMake configure 时 vcpkg 从 GitHub 拉源码 | 见 D-8；优先 Vulkan SDK 自带；必要时换 vcpkg baseline 或加 git mirror |
| P-7 | `u8"中文"` 在 C++20 报 C2664 类型不匹配 ImGui `const char*` | ImGui::Text / Begin 等 API 传字面量 | 项目用 `/utf-8` 编译，**普通 `"中文"` 即可**，不要加 `u8` 前缀；`char8_t` 与 `char` 是不同类型 |
| P-8 | Bash 工具 `cd vendor/<submodule>` 后续命令仍在子目录运行 | 子模块内 reset/查 status 后继续工作 | 用 `git -C "<repo-root>" ...` 显式指定主仓库根；或用绝对路径，禁止依赖 cwd 状态 |
| P-9 | 多次 `SpatialHashGrid::BuildVulkan` 同帧调用，pool Reset() 内置会让前次 set 失效 → cmd submit UAF | PCISPH 8 迭代每次内部 Grid.Build / 同帧粒子+流体共享 Grid | pool reset 移出 BuildVulkan；调用方每帧首次显式 `ResetFrameResources()`；pool 容量扩到 64 |
| P-10 | 主帧单 submit 只 signal 1 个 fence | AsyncReadback ring 需独立 fence | 不复用 swapchain inFlightFence；EndFrame 在主 submit 之后追加零 cmd submit 信号化 |
| P-11 | `VulkanIBLGenerator` 在 cubemap 不可用时 `m_IBLReady=true`（仅 BRDF LUT 完成，Irradiance/Prefilter 跳过），但 `GetXxxMapID()` 恒返回 0；`SkyboxSystem::HasIBL()` 在 Vulkan path 下会被误判为 true → 调用方拿到无效句柄 | Vulkan path 接通 SceneRenderer（Phase 8 PBR）时触发；当前 SmokeLayer 无 SceneRenderer 实例不触发 | Phase 8 前二选一：(a) `m_IBLReady` 改语义只有 Irradiance/Prefilter 也就绪才 true；(b) 改用 D-7 中的 `GetXxxView()` 接口，SceneRenderer 按 API 分派 |
| P-12 | `GrassRenderSystem::Init` 在 Vulkan path 下 `m_UseIndirectDraw=true` 但渲染时调 `RenderCommand::DrawArraysIndirect`（OpenGL-only），渲染将退化或崩 | Vulkan path 接通 Scene 渲染（Phase 8）时触发；当前 grass compute commit 走 SmokeLayer 验证不触发 | Phase 8 前在 GrassRenderSystem Init 内按 `RendererAPI::GetAPI()` 切换；或在 VulkanRendererAPI 实装 DrawArraysIndirect |
| P-13 | `VulkanShader` push constant 反射强制 offset=0 单段（取 `max(size)`） | 未来 shader 使用 `layout(offset=...)` 多段 push constant | 当前所有迁移 shader 都从 0 起始且单段，约束成立；新增多段 PC 时扩展 `m_ReflectedPushConstants` 为按 range 数组 |
| P-14 | 双路径 shader 改写时把 OpenGL `uniform vec3 u_EmitterPos` 改名/改类型 → cpp 端 `m_Shader->SetVec3("u_EmitterPos", ...)` 断裂 | Vulkan 迁移时为对齐宏命名一并改 OpenGL uniform 标识 | OpenGL 路径 `uniform 名 / 类型 / 个数`完全保留，新增 `#ifdef VULKAN` 分支独立写 `push_constant` / `UBO`，main 函数用 `#define` 宏（如 `MAX_PARTICLES` / `EMITTER_POS`）抹平两侧引用 |
| P-15 | 粒子/流体 Vulkan path 独立 `DescriptorPool` 每帧首次未 `Reset()` → set 累积溢出 pool 容量后 alloc 失败 | 每子系统在 cpp 内持有自己的 pool（独立于 `SpatialHashGrid::ResetFrameResources`） | 每帧首次 dispatch 前显式 `pool->Reset()`；本子系统的 pool reset 与 Grid 的 `ResetFrameResources()` 是两件事，不要互相覆盖职责 |

---

## 5. 全局约束

- 迁移**不影响 main 分支**：Editor 默认仍走 OpenGL；`Editor.exe --vulkan` 才进入 Vulkan 路径
- shader 双路径并存（`#ifdef VULKAN`），不允许删 OpenGL 分支
- 新 Vulkan 资源类必须经 VMA，禁止裸 `vkAllocateMemory`
- 每个迁移 commit 都应能单独构建通过（`cmake --build build --config RelWithDebInfo --target Editor`）
- 涉及 `.h`/`.cpp` 改动的 commit 在落盘前已被 `.claude/settings.json` 的 PreToolUse hook 自动 clang-format

---

## 6. Next Steps

按优先级，自上而下：

1. **Commit C — SPH 整体迁移（Phase 7 收尾）**
   - 已具备：B0/A0/A/B/D 全部基础设施 + 调用入口预埋（含 `m_SDFMetaReadback`）
   - 待做：
     - 7 个 SPH shader 加 `#ifdef VULKAN`（`sph_density` / `sph_force` / `sph_pcisph_{init,predict,density,force,apply}`）+ push constant 按计划 §Commit C 表
     - `FluidSystemGPU::UpdateSPHVulkan` 录入 PCISPH 1~8 迭代同帧 cmd（含中间 `m_Grid.BuildVulkan(cmd, alive, /*predicted=*/true)` 重建）
     - 回头补齐 `ParticleSystemGPU::UpdateVulkan` 的 SPH 分支（当前为 `ENGINE_CORE_WARN once + return`），接入 compact dispatch + Grid build
     - `FluidSystemGPU::UpdateVulkan` 内移除 SPH `static-once WARN`，PCISPH 接入后 `m_SDFMetaReadback` 路径自然生效
   - **协调点**：该 commit 同时触碰 `ParticleSystemGPU.cpp`（B 已落地）+ `FluidSystemGPU.cpp`（D 已落地），**串行执行**，不能再开 Subagent 并行
   - 验收：waterfall demo 在 `--vulkan` 路径下不崩、validation layer 0 error；`grep -n "RenderCommand::DispatchCompute" Engine/src/Renderer/{Particle,Fluid}SystemGPU.cpp` 仅命中 `RendererAPI::OpenGL` 分支

2. **`VulkanTextureCubemap` 实装**
   - 6 面 atlas 上传 + view + sampler
   - 解锁 `VulkanIBLGenerator` 真实输入（P-3）

3. **Phase 8：Vulkan PBR pass**
   - `VulkanIBLGenerator::GetXxxView()` 接入 PBR 采样
   - `VulkanContext::RenderImGui()` 实装（解锁 Editor 主路径）
   - P-11/P-12 在主路径接通前需先消化（IBL `HasIBL()` 语义 + Grass `DrawArraysIndirect` Vulkan 实装）

---

## 7. 收尾 Checklist（每次 session 结束前）

- [ ] 已完成项的 `- [ ]` 是否已改为 `- [x]`
- [ ] 新出现的非显而易见选择是否已写进 §3 Decision Log
- [ ] 新发现的踩坑是否已写进 §4 Pitfalls
- [ ] §1 "最近一次 commit" 是否已更新
- [ ] 当前 phase 待办若已重排序，§6 Next Steps 是否同步
