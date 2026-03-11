#pragma once

#include "Core/Base.h"

#include <cstdint>
#include <vector>

namespace Engine
{

    enum class FramebufferTextureFormat
    {
        None = 0,

        // Color
        RGBA8,
        RGBA16F,
        RED_INTEGER,
        R32F,
        R16F,

        // Depth/stencil
        DEPTH24STENCIL8,
        DEPTH_COMPONENT, // Sampable depth texture (for shadow maps)

        // Defaults
        Depth = DEPTH24STENCIL8
    };

    struct FramebufferTextureSpecification
    {
        FramebufferTextureSpecification() = default;
        FramebufferTextureSpecification(FramebufferTextureFormat format) : TextureFormat(format) {}

        FramebufferTextureFormat TextureFormat = FramebufferTextureFormat::None;
    };

    struct FramebufferAttachmentSpecification
    {
        FramebufferAttachmentSpecification() = default;
        FramebufferAttachmentSpecification(std::initializer_list<FramebufferTextureSpecification> attachments)
            : Attachments(attachments)
        {
        }

        std::vector<FramebufferTextureSpecification> Attachments;
    };

    struct FramebufferSpecification
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        FramebufferAttachmentSpecification Attachments;
        uint32_t Samples = 1;
        bool SwapChainTarget = false;
    };

    class Framebuffer
    {
    public:
        virtual ~Framebuffer() = default;

        virtual void Bind() = 0;
        virtual void Unbind() = 0;

        virtual void Resize(uint32_t width, uint32_t height) = 0;

        virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) = 0;
        virtual void ClearAttachment(uint32_t index, int value) = 0;

        virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const = 0;
        virtual uint32_t GetDepthAttachmentRendererID() const = 0;
        virtual const FramebufferSpecification& GetSpecification() const = 0;

        // MSAA support
        virtual void BindMSAA() = 0;
        virtual void BlitMSAA() = 0;
        virtual bool IsMSAAEnabled() const = 0;

        static Ref<Framebuffer> Create(const FramebufferSpecification& spec);
    };

} // namespace Engine
