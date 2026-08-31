# Engine/Platform 三后端简化审计（02-platform-backends）

> 行号基于 2026-08-31 工作区状态。实施前先重新定位。
> 跨后端（OpenGL vs Vulkan 各自实现 IBL/Framebuffer 等）是架构设计，**不算重复**；本文只收"同一后端内部"的重复。
> 风险标记：🟢 低 / 🟡 中 / 🔴 高（不建议动）

---

# V. Vulkan（约 8500 行）

## V1 🟡 VulkanTexture.cpp —— 2D 与 Cubemap 两个类内部复制粘贴（本文件收益最高）

- **位置**：`Engine/Platform/Vulkan/VulkanTexture.cpp`
  - `218-239` vs `534-555`：CreateSampler 逐字段相同
  - `241-292` vs `557-614`：staging 上传（vmaCreateBuffer(TRANSFER_SRC) → map → memcpy → copyBufferToImage → destroy）
  - `294-343` vs `616-658`：TransitionLayout（barrier 填法几乎相同）
  - `345-374` vs `660-688`：**Destroy 内的 DeferDestroy lambda 两份完全一致**
- **类别**：冗余代码
- **问题**：同一后端文件内两个类各自维护一套几乎相同的 sampler/上传/布局转换/延迟销毁实现。
- **简化方案**：文件匿名命名空间提取四个自由函数；两类的真实差异只有两处，用参数表达——
  - Cubemap `layerCount = 6`
  - Cubemap 布局转换的 dstStage 多一位 `COMPUTE`

```cpp
namespace
{
    void CreateSamplerImpl(VkDevice device, VkFilter filter, VkSamplerAddressMode addressMode, VkSampler& out);
    void UploadViaStagingImage(VkDevice device, VkImage image, VkExtent3D extent, uint32_t layerCount,
                               const void* data, VkDeviceSize bytes);
    void TransitionImageLayout(VkDevice device, VkImage image, uint32_t layerCount, VkImageLayout oldLayout,
                               VkImageLayout newLayout, VkPipelineStageFlags dstStageExtra);
}
// 两个类的成员函数各自缩为一行调用 + 差异参数（6 / VK_PIPELINE_STAGE_COMPUTE_BIT）
```

- **预计收益**：约 130-160 行
- **风险**：🟡 中——纯重构但量大（4 组 × 2 类）；mip 级数、aspect、queue family index 等隐式差异必须逐字段比对后才能收敛。单独立分支，纹理内容目检（PBR 材质 + 天空盒 + IBL 全过一遍）。**列为 P2。**

## V2 🟢 VulkanRendererAPI.cpp —— DrawIndexed/DrawArrays/DrawArraysInstanced 三份相同的 DrawParams 填充

- **位置**：`Engine/Platform/Vulkan/VulkanRendererAPI.cpp:119-199`、`201-241`、`243-283`
- **类别**：冗余代码 / 数据流
- **问题**：三个 Draw 函数各自重复：context 判空 + warn-once（3 份）、`DrawParams` 8 字段填充（3 份）、`dynamic_cast<VulkanShader*>` + `DispatchDraw`（3 份）。已亲读确认（119-199 为 DrawIndexed 全文）。
- **简化方案**（重构后完整代码）：

```cpp
namespace
{
    // 统一绘制分发：成功返回 true；失败/条件不满足返回 false（调用方走 fallback 队列）
    bool TryDispatchSceneDraw(VulkanRendererAPI* self, const Ref<VertexArray>& vertexArray, bool indexed,
                              uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset,
                              uint32_t instanceCount)
    {
        auto* vkContext = VulkanContext::Get();
        if (!vkContext)
            return false;

        const VkCommandBuffer cmd = vkContext->GetCurrentFrameCommandBuffer();
        if (cmd == VK_NULL_HANDLE || vkContext->GetActiveSceneRenderPass() == VK_NULL_HANDLE)
            return false;

        VulkanSceneDrawDispatcher::DrawParams params{};
        params.Cmd                  = cmd;
        params.RenderPass           = vkContext->GetActiveSceneRenderPass();
        params.ColorAttachmentCount = vkContext->GetActiveSceneColorAttachmentCount();
        params.Indexed              = indexed;
        params.IndexCount           = indexCount;
        params.FirstIndex           = firstIndex;
        params.VertexOffset         = vertexOffset;
        params.DepthTest            = self->m_DepthTestEnabled && self->m_DepthMaskEnabled;
        params.DepthWrite           = self->m_DepthMaskEnabled;
        params.DepthLEqual          = (self->m_DepthFunc == DepthFunc::LEqual);
        params.CullBack             = self->m_CullFaceEnabled && self->m_CullFaceMode == CullFaceMode::Back;

        auto* shader = dynamic_cast<VulkanShader*>(vkContext->GetSceneState().GetCurrentShader());
        return shader && vkContext->GetSceneDrawDispatcher().DispatchDraw(
                             vertexArray.get(), shader, params, vkContext->GetCurrentFrameIndex());
    }

    // 9 处 static bool warnedXxx 合并：
    void WarnOnce(const char* what)
    {
        static std::unordered_set<const char*> warned;
        if (warned.insert(what).second)
            ENGINE_CORE_WARN("[Vulkan] {0}", what);
    }
} // namespace

void VulkanRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
    uint32_t resolvedIndexCount = indexCount;
    uint32_t firstIndex         = 0;
    int32_t  vertexOffset       = 0;

    if (resolvedIndexCount == 0)
    {
        if (!vertexArray)
        {
            WarnOnce("DrawIndexed skipped because VertexArray is null and indexCount is 0");
            return;
        }
        const Ref<IndexBuffer>& indexBuffer = vertexArray->GetIndexBuffer();
        if (!indexBuffer)
        {
            WarnOnce("DrawIndexed skipped because IndexBuffer is missing");
            return;
        }
        resolvedIndexCount = indexBuffer->GetCount();
    }
    if (resolvedIndexCount == 0)
        return;

    if (TryDispatchSceneDraw(this, vertexArray, true, resolvedIndexCount, firstIndex, vertexOffset, 1))
        return;

    auto* vkContext = VulkanContext::Get();
    if (vkContext)
    {
        vkContext->QueueDrawIndexed(resolvedIndexCount, firstIndex, vertexOffset);
        WarnOnce("DrawIndexed fell back to non-indexed debug path");
    }
    else
        WarnOnce("DrawIndexed skipped because VulkanContext is unavailable");
}
// DrawArrays / DrawArraysInstanced 同法收敛（注意三者 fallback 队列函数名与 warn 文案保留各自原文）
```

- **前后对比**：三份约 80/40/40 行 → 辅助 1 份 + 各函数 25/12/14 行。
- **预计收益**：约 55-70 行
- **风险**：🟢 低——`DrawParams` 字段逐字段一致；`WarnOnce` 用字符串指针做 key（字面量常驻），行为与原 warn-once 等价。注意 `DrawArrays*` 的"无 IndexBuffer 解析"分支与 DrawIndexed 不同，保留在各自函数体。

## V3 🟢 VulkanContext.cpp —— 手写 barrier / 两份 framebuffer 循环 / 两份 messenger 填充

- **位置**：`VulkanContext.cpp:386-435`（RecordDefaultFramePasses 两段 15 行手写 `VkImageMemoryBarrier`）；`1134-1151` vs `1279-1294`（Debug 与 ImGui 两份 `VkFramebufferCreateInfo` 循环）；`525-539` vs `556-563`（debug messenger createInfo 两份填充）
- **类别**：冗余代码 / 可替换实现
- **问题**：本文件已 include `VulkanCommandBuffer.h`，其他文件（如 `VulkanFramebuffer::Invalidate`）都在用 `VulkanCommandBuffer::ImageBarrier`，这里却手写完整 barrier 结构体两遍；framebuffer 循环、messenger 填充各复制两份。
- **简化方案**：
  1. barrier 改用 `VulkanCommandBuffer(cmd).ImageBarrier(...)`（参数已齐全，语义逐位一致）；
  2. 提取 `CreateSwapchainFramebuffers(VkRenderPass, std::vector<VkFramebuffer>&)`；
  3. 提取 `FillDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT&)`。
- **预计收益**：约 55 行
- **风险**：🟢 低。barrier 的 src/dstStage 与 access 掩码照抄原值；⚠️ 替换后用 RenderDoc 抽一帧确认 swapchain 图像布局转换时序不变（本条在敏感区 BeginFrame/EndFrame **附近**但不在其中，只动 RecordDefaultFramePasses）。

## V4 🟢 Vulkan 死代码一批（全部 grep 验证，仅定义/声明命中）

- **位置与证据**（grep 范围 `Engine/Platform` + `Engine/src` + `Editor/src`，模式 `--include=*.cpp --include=*.h`）：

| 符号 | 位置 | 命中情况 |
|---|---|---|
| `VulkanShader::GetSpirv` | VulkanShader.cpp:129, .h:40 | 仅定义/声明 |
| `VulkanShader::GetIntArrayUniforms` + `m_IntArrayUniforms` | .h:76,106, .cpp:387 | getter 零调用；成员仅被 `SetIntArray` 写入，`PackAndUploadGlobals` 只消费 Int/Float/Float2-4/Mat3-4——**只写不读** |
| `VulkanCommandBuffer::SetHandle` | VulkanCommandBuffer.cpp:11, .h:16 | 仅定义/声明 |
| `VulkanPipelineCache::Contains` | VulkanPipelineCache.cpp:54 | 仅定义 |
| `VulkanSceneState::UnbindTextureSlot` | VulkanSceneState.cpp:31, .h:63 | 仅定义（`ResetTextureSlots` 已覆盖用途） |
| `VulkanContext::GetCommandPool` | VulkanContext.h:90 | 仅声明（成员 `m_CommandPool` 内部使用，删的只是访问器） |
| `VulkanContext::GetComputeQueue` / `GetComputeQueueFamily` / `HasDedicatedComputeQueue` | VulkanContext.h:72-74 | 仅声明 |
| `VulkanContext::GetCurrentImageIndex` | VulkanContext.h:111 | 仅声明 + 一条注释提及 |

- **类别**：死代码
- **简化方案**：删除上述函数与成员。`SetIntArray` 是 `Shader` 接口的 override，**保留**，但实现可改为空（不再存表），附注释说明 Vulkan 全局块无整型数组布局。
- **预计收益**：约 45-60 行
- **风险**：🟢 低（inline 访问器；`m_ComputeQueueFamily` 成员本身在 CreateLogicalDevice 内部使用，保留成员、只删访问器）。若确认无 async compute 计划，PickPhysicalDevice 里约 25 行 dedicated-compute 挑选逻辑可进一步简化为复用 graphics family——**待确认，单独列 P3**。

## V5 🟢 VulkanSceneDrawDispatcher.cpp —— phase-8.2 遗留调试日志块

- **位置**：`VulkanSceneDrawDispatcher.cpp:804-824`（`[DbgGenericUBO]` MISS/HIT 两段）、`868-887`（`[DbgBindLayout]` layout 名 switch）
- **类别**：冗余代码（诊断遗留）
- **问题**：排查期 WARN 级一次性日志（含静态去重集合）留在每帧绘制热路径；`[DbgGenericUBO] write` 对正常路径也打 WARN。当前分支（feature/vulkan-drawindexed）已结案草地黑剪影问题，这些日志的排查使命已完成。
- **简化方案**：直接删除；若想保留排查能力，统一改为 `ENGINE_VULKAN_DEBUG_BIND=1` 环境变量门控。
- **预计收益**：约 40 行 + 每帧 draw 少 3 次静态集合查找
- **风险**：🟢 低（纯日志）。**与用户确认不再需要排查"绑定布局分歧"后删除。**

## V6 🟡 VulkanBuffer.cpp —— staging 上传/回读样板分散 10 处

- **位置**：`VulkanBuffer.cpp:66-113`（UploadViaStaging）、`119-159`（DownloadViaStaging）；同型代码还出现在 `VulkanTexture.cpp:250-268/569-587`、`VulkanSceneDrawDispatcher.cpp:353-371`、`VulkanAsyncReadback.cpp:32-53`
- **类别**：冗余代码
- **问题**：全仓库 10 处 `vmaCreateBuffer(TRANSFER_SRC, AUTO_PREFER_HOST, SEQUENTIAL_WRITE)` + map + memcpy + unmap 样板。
- **简化方案**：在 VulkanAllocator 增加两个静态助手：

```cpp
// 返回 RAII 守卫：析构 vmaDestroyBuffer
struct ScopedStagingBuffer
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;
    void*   Mapped   = nullptr;
    ~ScopedStagingBuffer(); // unmap + destroy
};
static ScopedStagingBuffer CreateStagingBuffer(VkDeviceSize bytes);
static void UploadViaStaging(VkBufferImageCopy/*或 VkBufferCopy*/ region, ...); // 按目标类型二选一或两个重载
```

- **预计收益**：约 80-100 行
- **风险**：🟡 中——**触及上传热路径语义**（barrier 插入点、queue ownership）。逐调用点回归：纹理加载、SSBO 初始数据、读back 结果正确性。**列为 P2 末尾，甚至可不做。**

## V7 🟢 VulkanFramebuffer.cpp —— Invalidate 中 color/depth 附件创建重复

- **位置**：`VulkanFramebuffer.cpp:118-156` vs `159-196`
- **类别**：冗余代码
- **问题**：color 循环体与 depth 块的 image + view 创建仅 usage/aspect 两处不同。
- **简化方案**：提取

```cpp
void CreateAttachment(VmaAllocator allocator, VkFormat format, uint32_t width, uint32_t height,
                      VkImageUsageFlags usage, VkImageAspectFlags aspect, VkSampleCountFlagBits samples,
                      FramebufferAttachment& out);
```

color 循环与 depth 块各一行调用。
- **预计收益**：约 35 行
- **风险**：🟢 低。

## V8 🟢 VulkanIBLGenerator.cpp —— Shutdown 三段 pipeline 销毁复制

- **位置**：`VulkanIBLGenerator.cpp:141-155`
- **类别**：冗余代码
- **问题**：同一个 handle 三次逐字段销毁。
- **简化方案**（重构后完整代码）：

```cpp
for (auto* pipeline : {&m_BRDFLutPipeline, &m_IrradiancePipeline, &m_PrefilterPipeline})
{
    VulkanComputePipelineHandle handle{pipeline->Pipeline, pipeline->Layout};
    VulkanPipeline::DestroyCompute(device, handle);
    *pipeline = {};
}
```

- **预计收益**：约 12 行
- **风险**：🟢 低（shutdown 一次性路径）。

---

# O. OpenGL（约 2200 行）

## O1 🟡 OpenGLIBLGenerator.cpp —— Irradiance 与 Prefilter 生成块重复

- **位置**：`OpenGLIBLGenerator.cpp:177-242` vs `244-330`；行重排 memcpy 循环第三次出现在 `CreateEnvAtlas:129-134`
- **类别**：冗余代码
- **问题**：两块均为"建 cubemap → 建临时 2D atlas → 绑 image → dispatch → glGetTexImage 回读 → 行重排写入 6 面"，仅 mip 粒度与 roughness 参数不同。
- **简化方案**：提取

```cpp
void GenerateAtlasToCubemap(Shader* shader, uint32_t outCubemap, int size, int mipLevel, float roughness);
void CopyAtlasRows(const uint8_t* src, uint8_t* dst, int size, int face, size_t elemSize);
```

Irradiance 变成单 mip 特例调用。
- **预计收益**：约 80-90 行
- **风险**：🟡 中——低频离线预计算路径，但结果直接进天空盒/环境光照；**实施后对比一次 IBL 生成结果（截图目检 + 同场景对比）**。列为 P2。

## O2 🟢 OpenGLFramebuffer.cpp —— 析构与 Invalidate 重复清理块

- **位置**：`OpenGLFramebuffer.cpp:79-92` vs `97-120`
- **类别**：冗余代码
- **问题**：析构与重建各写一遍 glDelete FBO/textures/renderbuffers/MSAA。
- **简化方案**：提取私有 `ReleaseGpuResources()`（MSAA 清理并入），析构与 Invalidate 各调一次。
- **预计收益**：约 16 行
- **风险**：🟢 低。（Invalidate 中 `glBindTexture(GL_TEXTURE_2D, 0)` 缺失属行为问题，不属简化范围，单独走 bug 修复。）

## O3 🟡 OpenGLFramebuffer.cpp —— Legacy 单附件路径（待确认）

- **位置**：`OpenGLFramebuffer.cpp:218-237`
- **类别**：死代码（待确认）
- **问题**：仅当 `FramebufferSpecification` 无 Attachments 时触发；全仓库唯一触发点是 `Sandbox/src/SandboxApp.cpp:22`。Sandbox 仍在构建目标中（CMakeLists.txt:60）。
- **简化方案**：若 Sandbox 不再用于答辩演示，给 SandboxApp 的 fbSpec 填标准 attachments 后删除整段 legacy 路径；否则保留。
- **预计收益**：约 20 行
- **风险**：🟡 中（取决于 Sandbox 去留）——**待用户拍板（P3）**。

---

# C. CUDA —— 明确不动

## C1 🔴 CudaSPHPipeline.cu / CudaParticlePipeline.cu 的 kernel 近重复

- **位置**：`CudaSPHPipeline.cu:595-849`（Density/Force）vs `1031-1315`（PCISPHDensity/PCISPHForce）；粒子与流体 hash/emit kernel 亦有同构。
- **说明**：邻居遍历骨架、Akinci 法线预计算段在 4 个大 kernel 中重复，理论上可用 `__device__` 内联抽取，预计 150-200 行。**但**：kernel 合并会改变寄存器分配与占用率，数值路径是论文核心数据来源（WCSPH 50k L∞ 误伤问题尚待论文说明）。**答辩前禁止触碰**，写入总览禁止触碰清单。

---

# 本文档小计

| 区域 | 条目 | 预计行数 | 低风险部分 |
|---|---|---|---|
| Vulkan | V1-V8 | 480-600 | ~180（V2/V3/V4/V5/V7/V8） |
| OpenGL | O1-O3 | 115-125 | ~16（O2） |
| CUDA | C1 | （150-200 潜力，**不做**） | 0 |
| **合计** | 12 条 | **600-800** | **~180** |

优先动手顺序：V4（死代码批删）→ V5（调试日志）→ V2/V3/V7/V8（机械去重）→ O2 → V1/O1（两大块合并，单独立分支 + 回归）→ V6（最后甚至不做）。
