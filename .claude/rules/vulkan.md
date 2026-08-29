---
paths:
  - "Engine/Platform/Vulkan/**"
---

# Platform/Vulkan

`Engine/src/Renderer/` 中抽象接口的 Vulkan 1.2+ 具体实现，已合并到主分支（39 个文件）。

> **事实源**: `docs/vulkan-migration/SPEC.md`（实时进度 + 决策日志）。修改前先读。
> **阶段定义**: `docs/vulkan-migration-roadmap.md`（长期不变）。

## 启用方式

- CMake preset: `vs2022-vulkan`（需 Vulkan SDK 1.4+，环境变量 `VULKAN_SDK`）
- 运行时: `Editor.exe --vulkan` 触发 `RendererAPI::SetAPI(API::Vulkan)` 与 `RenderCommand` 延迟初始化
- 不指定参数 → 继续走 OpenGL 路径（main 分支行为零回归）

## 文件映射

| 抽象接口 | Vulkan 实现 |
|----------|-------------|
| RendererAPI | VulkanRendererAPI |
| GraphicsContext | VulkanContext（Instance/Device/Swapchain/CommandPool/Sync） |
| Buffer (VBO/IBO) | VulkanBuffer（via VMA） |
| StorageBuffer (SSBO) | VulkanStorageBuffer（6 种构造变体） |
| UniformBuffer | VulkanUniformBuffer |
| Texture2D | VulkanTexture2D（VMA Image + view + sampler + staging） |
| TextureCubemap | VulkanTextureCubemap（已实装：stbi 加载 6 面 + magenta fallback，见 `VulkanTexture.cpp:344`） |
| Framebuffer | VulkanFramebuffer（VkRenderPass + 多附件） |
| VertexArray | VulkanVertexArray（已接入 `VertexArray::Create` 工厂分派） |
| Shader | VulkanShader（shaderc 编译 + spirv-cross 反射） |
| IBLGenerator | VulkanIBLGenerator（3 个 compute dispatch） |
| — | VulkanCommandBuffer（录制抽象） |
| — | VulkanRenderPass / VulkanPipeline / VulkanSynchronization |
| — | VulkanDescriptor（SetLayout / Pool / Writer 三层） |
| — | VulkanAllocator（VMA allocator 生命周期） |
| — | VulkanBarrierUtil（`BarrierBit::*` → stage/access 映射） |
| — | VulkanGraphicsPipelineBuilder（场景 PSO 组装，已接入主帧渲染） |
| — | VulkanPipelineCache（key = shader + renderpass + 顶点布局 hash + 光栅状态位，已接入） |
| — | VulkanSceneState（GL 即时模式状态机模拟：当前 shader + 16 纹理槽） |
| — | VulkanSceneDrawDispatcher（DrawIndexed/DrawArrays 真实录制：std140 UBO 打包 + push constant + descriptor 写入） |
| — | VulkanDeletionQueue（按帧槽位延迟销毁，运行期资源释放必须走它） |
| — | VulkanAsyncReadback（3 槽 ring 异步回读） |

## 关键决策（摘自 SPEC.md §3）

- **D-1 push_constant 优先**: ≤128 bytes 的高频常量走 push constant，不要新建 UBO
- **D-2 GLSL 单源双路径**: 所有迁移 shader 用 `#ifdef VULKAN` 切换，OpenGL 分支不动；`VulkanShader.cpp` 在 shaderc 编译选项注入 `VULKAN=1` macro
- **D-3 SingleTime 限于低频路径**: IBL Init / 视角触发 rebuild 可用 `BeginSingleTimeCommands`；每帧 dispatch 必须录入主帧 command buffer
- **D-4 AsyncReadback ring**: 粒子/流体回读不要复制 `vmaMapMemory` 同步阻塞模式（grassCount 因低频特批）
- **D-5 SpatialHashGrid 外部 buffer**: 调用方必须先 `SetExternalBuffers(...)` 注入 particlePool / aliveList，再迁移自身 compute
- **D-6 ExternalMemoryHint 占位**: `VulkanStorageBuffer::ExternalMemoryHint::CudaInterop` 当前断言未实现，实装时机由 CUDA sidecar 迁移驱动
- **D-7 IBL view 接口**: `GetIrradianceView() / GetPrefilterView() / GetBRDFLutView()` 是目标接口；不要 cast `VkImageView` 到 `uint32_t`
- **D-17 上层零改动**: 新增场景功能按 OpenGL 语义写上层代码（Bind → SetUniform → Draw），由 `VulkanSceneState` + `VulkanSceneDrawDispatcher` 自动快照打包录制；不要在 Vulkan path 直接录 cmd，dispatcher 不支持的模式（instanced/SSBO）在 dispatcher 内扩展
- **D-18 延迟销毁**: 运行期 Vulkan 资源析构一律经 `VulkanContext::DeferDestroy`（deletion queue 按帧槽位），禁止录制窗口内直接 `vkDestroy*`
- **D-19 validation 门控**: Debug 默认开 / Release 关，`ENGINE_VULKAN_VALIDATION=1` 环境变量可强制开启排查

## 已知陷阱（摘自 SPEC.md §4）

- prefix_sum 三 pass 之间 `ResolveBarrierBits(ShaderStorage)` barrier 不能省
- 每帧路径调用 `BeginSingleTimeCommands` 会 stall 主帧 → 见 D-3
- `--vulkan` 已走 EditorLayer 主路径（Phase 8 双后端接通，见 `Editor/src/EditorApp.cpp:23-26` 注释）；`VulkanSmokeLayer.{h,cpp}` 为遗留死代码，勿再引用

## 注意事项

- 所有 Vulkan 资源走 **VMA**（VulkanAllocator），禁止裸 `vkAllocateMemory`
- Descriptor set 三层抽象：`VulkanDescriptorSetLayout`（layout） / `VulkanDescriptorPool`（per-frame reset 复用） / `VulkanDescriptorWriter`（Builder 风格累积 writes）
- `VulkanShader` 暴露 SPIR-V 字节码 + 懒创建 `VkShaderModule` 缓存 + 反射 binding / push constant
- `VulkanShader` 的 `SetXxx` 不再是 no-op：值记录到 CPU 侧 `unordered_map`（8 组 `GetXxxUniforms()` getter，见 `VulkanShader.h:68-81`），供后续 UBO 打包 + descriptor 写入；本阶段不真正上传
- `VulkanPipelineCache`：key = `{Shader*, VkRenderPass, ColorAttachmentCount, VertexLayoutHash, Depth/Cull 状态位}`，`GetOrCreate` 懒创建，entry 持有 pipeline+layout 所有权；已接入主帧渲染（Phase 8.2）
- Memory barrier 通过 `VulkanBarrierUtil::ResolveBarrierBits(bits)` 将 `BarrierBit::{ShaderStorage|Command|BufferUpdate|All}` 解析为 `VkPipelineStageFlags` + `VkAccessFlags` 四元组（多 bit 取并集）
- ImGui 通过 `imgui_impl_vulkan` 初始化，独立 RenderPass + 自动 descriptor pool
- IBL 输出经 `GetIrradianceView()/GetPrefilterView()/GetBRDFLutView()`（`VulkanIBLGenerator.h:45-47`）以 `void*` 透传 VkImageView，`SceneRenderer` 按 API 分派 ID vs View（`RenderCommand::BindCubemapView`）
- `VulkanIBLGenerator::Generate(skybox)` 已消费真实 `VulkanTextureCubemap`（view/sampler），skybox 无效时只生成 BRDF LUT 并跳过 Irradiance/Prefilter（`VulkanIBLGenerator.h:23-24` 注释）
- 每个迁移 commit 必须能单独构建通过（`cmake --build build --config RelWithDebInfo --target Editor`）
- 新增 compute shader 时双路径并存，`#ifdef VULKAN` 分支用 push constant 替代 default uniform
