#include "engpch.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanSceneDrawDispatcher.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Renderer/RendererCapabilities.h"

#include <sstream>
#include <vector>

namespace Engine
{

    namespace
    {
        const char* VendorIdToString(uint32_t vendorId)
        {
            switch (vendorId)
            {
            case 0x10DE:
                return "NVIDIA";
            case 0x1002:
                return "AMD";
            case 0x8086:
                return "Intel";
            case 0x106B:
                return "Apple";
            case 0x5143:
                return "Qualcomm";
            case 0x13B5:
                return "ARM";
            case 0x1010:
                return "Imagination Technologies";
            default:
                return nullptr;
            }
        }

        std::string VendorString(uint32_t vendorId)
        {
            if (const char* known = VendorIdToString(vendorId))
                return known;

            std::ostringstream oss;
            oss << "UnknownVendor(0x" << std::hex << vendorId << ")";
            return oss.str();
        }

        void WarnUnsupportedOnce(const char* feature)
        {
            ENGINE_CORE_WARN("[Vulkan] {} is not implemented yet (Phase 3 in progress)", feature);
        }
    } // namespace

    void VulkanRendererAPI::Init()
    {
        auto* vkContext = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(vkContext, "VulkanContext must be initialized before VulkanRendererAPI::Init");
        vkContext->SetClearColor(m_ClearColor);
    }

    void VulkanRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        auto* vkContext = VulkanContext::Get();
        if (vkContext)
            vkContext->SetViewport(x, y, width, height);
    }

    void VulkanRendererAPI::SetClearColor(const glm::vec4& color)
    {
        m_ClearColor = color;

        auto* vkContext = VulkanContext::Get();
        if (vkContext)
            vkContext->SetClearColor(color);
    }

    void VulkanRendererAPI::Clear()
    {
        auto* vkContext = VulkanContext::Get();
        if (vkContext)
            vkContext->SetClearColor(m_ClearColor);
    }

    void VulkanRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
    {
        uint32_t resolvedIndexCount = indexCount;
        uint32_t firstIndex         = 0;
        int32_t  vertexOffset       = 0;

        if (resolvedIndexCount == 0)
        {
            if (!vertexArray)
            {
                static bool warnedMissingVertexArray = false;
                if (!warnedMissingVertexArray)
                {
                    warnedMissingVertexArray = true;
                    ENGINE_CORE_WARN("[Vulkan] DrawIndexed skipped because VertexArray is null and indexCount is 0");
                }
                return;
            }

            const Ref<IndexBuffer>& indexBuffer = vertexArray->GetIndexBuffer();
            if (!indexBuffer)
            {
                static bool warnedMissingIndexBuffer = false;
                if (!warnedMissingIndexBuffer)
                {
                    warnedMissingIndexBuffer = true;
                    ENGINE_CORE_WARN("[Vulkan] DrawIndexed skipped because IndexBuffer is missing");
                }
                return;
            }

            resolvedIndexCount = indexBuffer->GetCount();
        }

        if (resolvedIndexCount == 0)
            return;

        auto* vkContext = VulkanContext::Get();
        if (!vkContext)
        {
            static bool warnedNoContext = false;
            if (!warnedNoContext)
            {
                warnedNoContext = true;
                ENGINE_CORE_WARN("[Vulkan] DrawIndexed skipped because VulkanContext is unavailable");
            }
            return;
        }

        // Phase 8.2：场景 renderpass 激活时走真实 indexed 绘制（分发失败保留 debug fallback）
        const VkCommandBuffer cmd = vkContext->GetCurrentFrameCommandBuffer();
        if (cmd != VK_NULL_HANDLE && vkContext->GetActiveSceneRenderPass() != VK_NULL_HANDLE)
        {
            VulkanSceneDrawDispatcher::DrawParams params{};
            params.Cmd                  = cmd;
            params.RenderPass           = vkContext->GetActiveSceneRenderPass();
            params.ColorAttachmentCount = vkContext->GetActiveSceneColorAttachmentCount();
            params.Indexed              = true;
            params.IndexCount           = resolvedIndexCount;
            params.FirstIndex           = firstIndex;
            params.VertexOffset         = vertexOffset;
            params.DepthTest            = m_DepthTestEnabled && m_DepthMaskEnabled;
            params.DepthWrite           = m_DepthMaskEnabled;
            params.DepthLEqual          = (m_DepthFunc == DepthFunc::LEqual);
            params.CullBack             = m_CullFaceEnabled && m_CullFaceMode == CullFaceMode::Back;

            auto* shader = dynamic_cast<VulkanShader*>(vkContext->GetSceneState().GetCurrentShader());
            if (shader && vkContext->GetSceneDrawDispatcher().DispatchDraw(vertexArray.get(), shader, params,
                                                                           vkContext->GetCurrentFrameIndex()))
                return;
        }

        vkContext->QueueDrawIndexed(resolvedIndexCount, firstIndex, vertexOffset);

        static bool warnedIndexedFallback = false;
        if (!warnedIndexedFallback)
        {
            warnedIndexedFallback = true;
            ENGINE_CORE_WARN("[Vulkan] DrawIndexed fell back to non-indexed debug path");
        }
    }

    void VulkanRendererAPI::DrawArrays(uint32_t count, uint32_t first)
    {
        auto* vkContext = VulkanContext::Get();
        if (!vkContext)
        {
            static bool warnedNoContext = false;
            if (!warnedNoContext)
            {
                warnedNoContext = true;
                ENGINE_CORE_WARN("[Vulkan] DrawArrays skipped because VulkanContext is unavailable");
            }
            return;
        }

        // Phase 8.2：场景 renderpass 激活时走真实 non-indexed 绘制（天空盒等；
        // 抽象层无 VAO 参数，取状态机最近一次 Bind 的快照）
        const VkCommandBuffer cmd      = vkContext->GetCurrentFrameCommandBuffer();
        const VertexArray*    boundVAO = vkContext->GetSceneState().GetCurrentVertexArray();
        if (cmd != VK_NULL_HANDLE && vkContext->GetActiveSceneRenderPass() != VK_NULL_HANDLE && boundVAO)
        {
            VulkanSceneDrawDispatcher::DrawParams params{};
            params.Cmd                  = cmd;
            params.RenderPass           = vkContext->GetActiveSceneRenderPass();
            params.ColorAttachmentCount = vkContext->GetActiveSceneColorAttachmentCount();
            params.Indexed              = false;
            params.VertexCount          = count;
            params.FirstVertex          = first;
            params.DepthTest            = m_DepthTestEnabled && m_DepthMaskEnabled;
            params.DepthWrite           = m_DepthMaskEnabled;
            params.DepthLEqual          = (m_DepthFunc == DepthFunc::LEqual);
            // 天空盒画在深度 ≤ 上（xyww trick），关闭背面剔除由 SkyboxSystem 的 cull 状态决定
            params.CullBack = m_CullFaceEnabled && m_CullFaceMode == CullFaceMode::Back;

            auto* shader = dynamic_cast<VulkanShader*>(vkContext->GetSceneState().GetCurrentShader());
            if (shader && vkContext->GetSceneDrawDispatcher().DispatchDraw(boundVAO, shader, params,
                                                                           vkContext->GetCurrentFrameIndex()))
                return;
        }

        vkContext->QueueDrawArrays(count, first);
    }

    void VulkanRendererAPI::DrawArraysInstanced(uint32_t count, uint32_t instanceCount, uint32_t first)
    {
        auto* vkContext = VulkanContext::Get();
        if (!vkContext)
        {
            static bool warnedNoContext = false;
            if (!warnedNoContext)
            {
                warnedNoContext = true;
                ENGINE_CORE_WARN("[Vulkan] DrawArraysInstanced skipped because VulkanContext is unavailable");
            }
            return;
        }

        // Phase 8.2：instanced 真实绘制未接通（粒子 billboard 需要 SSBO 注册表，
        // 属后续阶段）。场景 pass 激活时直接丢弃——若进 pending 队列会被 debug
        // pass 用 DebugTriangle 管线画满 swapchain（粒子场景上万 instance 铺屏）。
        if (vkContext->GetActiveSceneRenderPass() != VK_NULL_HANDLE)
        {
            static bool warnedDropped = false;
            if (!warnedDropped)
            {
                warnedDropped = true;
                ENGINE_CORE_WARN("[Vulkan] DrawArraysInstanced not wired to scene path yet; draw dropped");
            }
            return;
        }

        vkContext->QueueDrawArraysInstanced(count, instanceCount, first);
    }

    void VulkanRendererAPI::DrawLines(uint32_t count, uint32_t first)
    {
        auto* vkContext = VulkanContext::Get();
        if (vkContext)
        {
            vkContext->QueueDrawLines(count, first);
            return;
        }

        static bool warnedNoContext = false;
        if (!warnedNoContext)
        {
            warnedNoContext = true;
            ENGINE_CORE_WARN("[Vulkan] DrawLines skipped because VulkanContext is unavailable");
        }
    }

    void VulkanRendererAPI::SetDepthTest(bool enable)
    {
        m_DepthTestEnabled = enable;
    }

    void VulkanRendererAPI::SetDepthFunc(DepthFunc func)
    {
        m_DepthFunc = func;
    }

    void VulkanRendererAPI::SetCullFace(bool enable)
    {
        m_CullFaceEnabled = enable;
    }

    void VulkanRendererAPI::SetCullFaceMode(CullFaceMode mode)
    {
        m_CullFaceMode = mode;
    }

    void VulkanRendererAPI::SetLineWidth(float width)
    {
        m_LineWidth = width;
    }

    void VulkanRendererAPI::BindTextureUnit(uint32_t slot, uint32_t textureID)
    {
        (void)slot;
        (void)textureID;
    }

    void VulkanRendererAPI::BindCubemapUnit(uint32_t slot, uint32_t textureID)
    {
        (void)slot;
        (void)textureID;
    }

    // Phase 8.2：view/sampler 为 VkImageView/VkSampler 直通（RendererAPI.h 抽象约定），
    // 写入场景状态机纹理槽，DrawIndexed 录制时消费
    void VulkanRendererAPI::BindTextureView(uint32_t slot, void* view, void* sampler)
    {
        if (auto* context = VulkanContext::Get())
            context->GetSceneState().BindTextureSlot(slot, static_cast<VkImageView>(view),
                                                     static_cast<VkSampler>(sampler));
    }

    void VulkanRendererAPI::BindCubemapView(uint32_t slot, void* view, void* sampler)
    {
        if (auto* context = VulkanContext::Get())
            context->GetSceneState().BindTextureSlot(slot, static_cast<VkImageView>(view),
                                                     static_cast<VkSampler>(sampler));
    }

    void VulkanRendererAPI::ClearColorOnly()
    {
        auto* vkContext = VulkanContext::Get();
        if (vkContext)
            vkContext->SetClearColor(m_ClearColor);
    }

    int VulkanRendererAPI::GetBoundFramebufferID()
    {
        return 0;
    }

    void VulkanRendererAPI::BindFramebufferByID(int id)
    {
        (void)id;
    }

    void VulkanRendererAPI::DispatchCompute(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        (void)groupsX;
        (void)groupsY;
        (void)groupsZ;
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            WarnUnsupportedOnce("DispatchCompute");
        }
    }

    void VulkanRendererAPI::MemoryBarrier(uint32_t barriers)
    {
        (void)barriers;
    }

    void VulkanRendererAPI::WaitIdle()
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext must be initialized before WaitIdle");
        vkDeviceWaitIdle(context->GetDevice());
    }

    void VulkanRendererAPI::DrawArraysIndirect(uint32_t bufferID)
    {
        (void)bufferID;
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            WarnUnsupportedOnce("DrawArraysIndirect");
        }
    }

    void VulkanRendererAPI::SetDepthMask(bool enable)
    {
        m_DepthMaskEnabled = enable;
    }

    void VulkanRendererAPI::SetBlendFunc(BlendFactor src, BlendFactor dst)
    {
        m_BlendSrc = src;
        m_BlendDst = dst;
    }

    void VulkanRendererAPI::SetBlend(bool enable)
    {
        m_BlendEnabled = enable;
    }

    bool VulkanRendererAPI::GetBlendEnabled()
    {
        return m_BlendEnabled;
    }

    void VulkanRendererAPI::SetScissorTest(bool enable)
    {
        m_ScissorTestEnabled = enable;
    }

    void VulkanRendererAPI::SetColorMask(bool r, bool g, bool b, bool a)
    {
        m_ColorMaskR = r;
        m_ColorMaskG = g;
        m_ColorMaskB = b;
        m_ColorMaskA = a;
    }

    glm::vec4 VulkanRendererAPI::GetClearColor()
    {
        return m_ClearColor;
    }

    void VulkanRendererAPI::SetReadBuffer(uint32_t attachment)
    {
        (void)attachment;
    }

    void VulkanRendererAPI::SetDrawBuffer(uint32_t attachment)
    {
        (void)attachment;
    }

    void VulkanRendererAPI::SetDrawBuffers(uint32_t count, const uint32_t* attachments)
    {
        (void)count;
        (void)attachments;
    }

    void VulkanRendererAPI::CopyFramebufferToTexture(uint32_t texID, uint32_t width, uint32_t height)
    {
        (void)texID;
        (void)width;
        (void)height;
    }

    void VulkanRendererAPI::QueryCapabilities(RendererCapabilities& caps)
    {
        auto* vkContext = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(vkContext, "VulkanContext must be initialized before QueryCapabilities");

        VkPhysicalDevice physicalDevice = vkContext->GetPhysicalDevice();
        ENGINE_CORE_RELEASE_ASSERT(physicalDevice != VK_NULL_HANDLE, "Vulkan physical device is null");

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);

        caps.MajorVersion   = static_cast<int>(VK_API_VERSION_MAJOR(props.apiVersion));
        caps.MinorVersion   = static_cast<int>(VK_API_VERSION_MINOR(props.apiVersion));
        caps.VendorString   = VendorString(props.vendorID);
        caps.RendererString = props.deviceName;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

        bool supportsCompute = false;
        for (const auto& queueFamily : queueFamilies)
        {
            if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0)
            {
                supportsCompute = true;
                break;
            }
        }

        caps.SupportsComputeShaders = supportsCompute;
    }

} // namespace Engine
