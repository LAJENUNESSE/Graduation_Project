#include "engpch.h"
#include "Renderer/Framebuffer.h"

#include "Core/Assert.h"
#include "Debug/GpuMemoryStats.h"
#include "Platform/OpenGL/OpenGLFramebuffer.h"
#include "Renderer/RendererAPI.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanFramebuffer.h"
#endif

namespace Engine
{

    Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec)
    {
        Ref<Framebuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLFramebuffer>(spec);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanFramebuffer>(spec);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "Vulkan support not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        // 交换链目标 FBO 不拥有引擎侧纹理，不计入显存账目
        if (!spec.SwapChainTarget)
        {
            uint64_t bytes = 0;
            for (const auto& attachment : spec.Attachments.Attachments)
                bytes += uint64_t(spec.Width) * spec.Height * GpuFramebufferFormatBPP(attachment.TextureFormat);
            bytes *= spec.Samples;

            std::string label = "FB " + std::to_string(spec.Width) + "x" + std::to_string(spec.Height);
            if (spec.Samples > 1)
                label += " MSAA" + std::to_string(spec.Samples);

            GpuMemoryStats::Get().TrackResource(ref, GpuMemCategory::FramebufferAttachment, bytes, label);
        }
        return ref;
    }

} // namespace Engine
