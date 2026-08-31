#include "engpch.h"
#include "Renderer/FluidRenderer.h"
#include "Core/Log.h"
#include "Renderer/Buffer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererAPI.h"
#include "Scene/Components.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanFramebuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#endif

namespace Engine
{

#ifdef ENGINE_ENABLE_VULKAN
    namespace
    {
        // std140 镜像 — 与 fluid_depth/thickness.glsl 的 FluidParticleVSUBO 块对应（144B）
        struct alignas(16) FluidVSParamsUBO
        {
            glm::mat4 View;                 // 0
            glm::mat4 Projection;           // 64
            glm::vec4 ParticleRadiusAndPad; // 128: x=ParticleRadius
        };
        static_assert(sizeof(FluidVSParamsUBO) == 144, "FluidVSParamsUBO must be std140 144 bytes");

        // std140 镜像 — 与 fluid_smooth.glsl 的 FluidSmoothParams 块对应（32B）
        struct alignas(16) FluidSmoothUBO
        {
            glm::vec4 ScreenSizeAndParams; // 0: xy=ScreenSize, z=FilterRadius, w=DepthFalloff
            glm::vec4 HorizontalAndPad;    // 16: x=Horizontal
        };
        static_assert(sizeof(FluidSmoothUBO) == 32, "FluidSmoothUBO must be std140 32 bytes");

        // std140 镜像 — 与 fluid_composite.glsl 的 FluidCompositeUBO 块对应（192B）
        struct alignas(16) FluidCompositeParamsUBO
        {
            glm::mat4 InvProjection;    // 0
            glm::mat4 InvView;          // 64
            glm::vec4 FluidColor;       // 128: xyz
            glm::vec4 AbsorptionColor;  // 144: xyz
            glm::vec4 ScreenSizeAndAbs; // 160: xy=ScreenSize, z=AbsorptionScale
            glm::vec4 SurfaceParams;    // 176: x=FresnelPower, y=RefractionStrength,
                                        //      z=Reflectivity, w=RefractiveIndex
        };
        static_assert(sizeof(FluidCompositeParamsUBO) == 192, "FluidCompositeParamsUBO must be std140 192 bytes");
    } // namespace
#endif

    void FluidRenderer::CreateFullscreenQuad()
    {
        // clang-format off
        float quadVertices[] = {
            // Position(2) + TexCoord(2)
            -1.0f, -1.0f,   0.0f, 0.0f,
             1.0f, -1.0f,   1.0f, 0.0f,
             1.0f,  1.0f,   1.0f, 1.0f,
            -1.0f,  1.0f,   0.0f, 1.0f,
        };

        uint32_t quadIndices[] = {0, 1, 2, 2, 3, 0};
        // clang-format on

        m_QuadVAO = VertexArray::Create();
        auto vb   = VertexBuffer::Create(quadVertices, sizeof(quadVertices));
        vb->SetLayout({
            {ShaderDataType::Float2, "a_Position"},
            {ShaderDataType::Float2, "a_TexCoord"},
        });
        m_QuadVAO->AddVertexBuffer(vb);

        auto ib = IndexBuffer::Create(quadIndices, 6);
        m_QuadVAO->SetIndexBuffer(ib);
    }

    void FluidRenderer::RenderFullscreenQuad()
    {
        m_QuadVAO->Bind();
        RenderCommand::DrawIndexed(m_QuadVAO);
    }

    void FluidRenderer::Init(uint32_t width, uint32_t height)
    {
        if (m_Initialized)
            return;

        m_Width  = width;
        m_Height = height;

        CreateFullscreenQuad();

        // Shaders
        m_DepthShader     = Shader::Create("assets/shaders/fluid_depth.glsl");
        m_SmoothShader    = Shader::Create("assets/shaders/fluid_smooth.glsl");
        m_ThicknessShader = Shader::Create("assets/shaders/fluid_thickness.glsl");
        m_CompositeShader = Shader::Create("assets/shaders/fluid_composite.glsl");

        // Depth FBO: R32F color + depth attachment
        {
            FramebufferSpecification spec;
            spec.Attachments = {FramebufferTextureFormat::R32F, FramebufferTextureFormat::DEPTH24STENCIL8};
            spec.Width       = width;
            spec.Height      = height;
            m_DepthFBO       = Framebuffer::Create(spec);
        }

        // Smooth FBOs: R32F ping-pong pair
        for (int i = 0; i < 2; i++)
        {
            FramebufferSpecification spec;
            spec.Attachments = {FramebufferTextureFormat::R32F};
            spec.Width       = width;
            spec.Height      = height;
            m_SmoothFBO[i]   = Framebuffer::Create(spec);
        }

        // Thickness FBO: R16F
        {
            FramebufferSpecification spec;
            spec.Attachments = {FramebufferTextureFormat::R16F};
            spec.Width       = width;
            spec.Height      = height;
            m_ThicknessFBO   = Framebuffer::Create(spec);
        }

        // Scene color copy texture (RGBA16F)
        {
            TextureSpecification spec;
            spec.Format         = TextureFormat::RGBA16F;
            spec.MinFilter      = TextureFilter::Linear;
            spec.MagFilter      = TextureFilter::Linear;
            spec.WrapS          = TextureWrap::ClampToEdge;
            spec.WrapT          = TextureWrap::ClampToEdge;
            spec.UploadLayout   = TextureDataLayout::RGBA_Float;
            m_SceneColorCopyTex = Texture2D::Create(width, height, spec);
        }

#ifdef ENGINE_ENABLE_VULKAN
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            // shader 大块参数走 std140 UBO（binding 与 GLSL Vulkan 分支声明一致）
            m_FluidVSUBO      = UniformBuffer::Create(sizeof(FluidVSParamsUBO), 1);
            m_SmoothParamsUBO = UniformBuffer::Create(sizeof(FluidSmoothUBO), 1);
            m_CompositeUBO    = UniformBuffer::Create(sizeof(FluidCompositeParamsUBO), 4);

            // u_SceneDepth 拷贝容器（depth-only FBO；HDR depth 在 composite 的 render
            // pass 内是 attachment layout，直接采样 descriptor layout 会冲突）
            FramebufferSpecification depthCopySpec;
            depthCopySpec.Attachments = {FramebufferTextureFormat::DEPTH_COMPONENT};
            depthCopySpec.Width       = width;
            depthCopySpec.Height      = height;
            m_SceneDepthCopyFBO       = Framebuffer::Create(depthCopySpec);
        }
#endif

        m_Initialized = true;
    }

    void FluidRenderer::Resize(uint32_t width, uint32_t height)
    {
        if (width == m_Width && height == m_Height)
            return;
        if (width == 0 || height == 0)
            return;

        m_Width  = width;
        m_Height = height;

        // 中间 FBO (Depth/Smooth/Thickness) 由 Render() 按 RenderScale 按需 resize，
        // 此处只更新全分辨率资源（scene color copy texture）。

        // Recreate scene color copy texture at new size
        {
            TextureSpecification spec;
            spec.Format         = TextureFormat::RGBA16F;
            spec.MinFilter      = TextureFilter::Linear;
            spec.MagFilter      = TextureFilter::Linear;
            spec.WrapS          = TextureWrap::ClampToEdge;
            spec.WrapT          = TextureWrap::ClampToEdge;
            spec.UploadLayout   = TextureDataLayout::RGBA_Float;
            m_SceneColorCopyTex = Texture2D::Create(width, height, spec);
        }
    }

    void FluidRenderer::Shutdown()
    {
        m_SceneColorCopyTex.reset();
#ifdef ENGINE_ENABLE_VULKAN
        m_FluidVSUBO.reset();
        m_SmoothParamsUBO.reset();
        m_CompositeUBO.reset();
        m_SceneDepthCopyFBO.reset();
#endif
        m_Initialized = false;
    }

    void FluidRenderer::Render(const Ref<ShaderStorageBuffer>& particleBuffer,
                               const Ref<VertexArray>&         emptyVAO,
                               uint32_t                        particleCount,
                               float                           particleRadius,
                               const glm::mat4&                view,
                               const glm::mat4&                projection,
                               uint32_t                        sceneColorTexID,
                               uint32_t                        sceneDepthTexID,
                               const FluidEmitterComponent&    emitter,
                               const Ref<Framebuffer>&         hdrTarget)
    {
        if (!m_Initialized || particleCount == 0)
            return;

#ifdef ENGINE_ENABLE_VULKAN
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            RenderVulkan(particleBuffer, emptyVAO, particleCount, particleRadius, view, projection, emitter, hdrTarget);
            return;
        }
#endif

        // Save caller's FBO and GL state
        int       callerFBO       = RenderCommand::GetBoundFramebufferID();
        glm::vec4 savedClearColor = RenderCommand::GetClearColor();

        // Bind particle buffer for instanced draw
        particleBuffer->Bind(0);

        glm::mat4 invProjection = glm::inverse(projection);
        glm::mat4 invView       = glm::inverse(view);

        // 可配置分辨率：Depth/Smooth/Thickness 在缩放分辨率下执行
        float    renderScale = std::clamp(emitter.RenderScale, 0.25f, 1.0f);
        uint32_t fluidW      = std::max(1u, static_cast<uint32_t>(m_Width * renderScale));
        uint32_t fluidH      = std::max(1u, static_cast<uint32_t>(m_Height * renderScale));

        // 按需 resize 中间 FBO
        m_DepthFBO->Resize(fluidW, fluidH);
        m_SmoothFBO[0]->Resize(fluidW, fluidH);
        m_SmoothFBO[1]->Resize(fluidW, fluidH);
        m_ThicknessFBO->Resize(fluidW, fluidH);

        // ================================================================
        // Pass 1: Depth — sphere impostor rendering to R32F
        // ================================================================
        m_DepthFBO->Bind();
        RenderCommand::SetViewport(0, 0, fluidW, fluidH);

        // Clear depth FBO to far value (1e10)
        RenderCommand::SetClearColor({1e10f, 0.0f, 0.0f, 0.0f});
        RenderCommand::Clear();

        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthFunc(DepthFunc::Less);
        RenderCommand::SetDepthMask(true);

        // 关键修复：depth pass 的 fragment shader 输出 `out float FragDepth` 写入 R32F，
        // alpha 通道未定义（NVIDIA 驱动可能为 0）。若上游 pass 把 GL_BLEND 开着且 func 为
        // SRC_ALPHA/ONE_MINUS_SRC_ALPHA，会导致 final.r = FragDepth*0 + clear*1 = 1e10，
        // 整张 depth 纹理永远保持 clear 值，composite shader 全屏走 early-return 看不见流体。
        // 在 depth pass 内强制禁用 blend，Pass 结束后恢复。
        bool prevBlend = RenderCommand::GetBlendEnabled();
        if (prevBlend)
            RenderCommand::SetBlend(false);

        m_DepthShader->Bind();
        m_DepthShader->SetMat4("u_View", view);
        m_DepthShader->SetMat4("u_Projection", projection);
        m_DepthShader->SetFloat("u_ParticleRadius", particleRadius);

        emptyVAO->Bind();
        RenderCommand::DrawArraysInstanced(6, particleCount);

        // 恢复 blend 状态（depth pass 前临时禁用的）
        if (prevBlend)
            RenderCommand::SetBlend(true);

        m_DepthFBO->Unbind();

        // ================================================================
        // Pass 2: Bilateral smooth — ping-pong blur
        // ================================================================
        uint32_t smoothInput = m_DepthFBO->GetColorAttachmentRendererID(0);

        int smoothIterations = std::max(emitter.SmoothIterations, 1);

        // 关键修复：禁用 blend + scissor + 全开 color mask，防止上游 pass（尤其是上一帧的 thickness
        // pass ONE/ONE blend）留下的 GL state 污染 smooth pass 的 R32F FBO 写入。R32F 没 alpha 通道，
        // 若 blend 开启且 func 为 SRC_ALPHA/ONE_MINUS_SRC_ALPHA，alpha=0 会让 shader 输出被完全屏蔽，
        // smooth FBO 保持 driver 初值 0，下游 composite shader 走 early-return 看不见流体。
        RenderCommand::SetBlend(false);
        RenderCommand::SetScissorTest(false);
        RenderCommand::SetColorMask(true, true, true, true);

        for (int i = 0; i < smoothIterations; i++)
        {
            // Horizontal pass
            m_SmoothFBO[0]->Bind();
            RenderCommand::SetViewport(0, 0, fluidW, fluidH);
            RenderCommand::SetDepthTest(false);

            m_SmoothShader->Bind();
            RenderCommand::BindTextureUnit(0, smoothInput);
            m_SmoothShader->SetInt("u_DepthTexture", 0);
            m_SmoothShader->SetInt("u_Horizontal", 1);
            m_SmoothShader->SetFloat2("u_ScreenSize", glm::vec2(fluidW, fluidH));
            m_SmoothShader->SetFloat("u_FilterRadius", emitter.SmoothFilterRadius);
            m_SmoothShader->SetFloat("u_DepthFalloff", emitter.SmoothDepthFalloff);
            RenderFullscreenQuad();

            m_SmoothFBO[0]->Unbind();

            // Vertical pass
            m_SmoothFBO[1]->Bind();
            RenderCommand::SetViewport(0, 0, fluidW, fluidH);

            m_SmoothShader->Bind();
            RenderCommand::BindTextureUnit(0, m_SmoothFBO[0]->GetColorAttachmentRendererID(0));
            m_SmoothShader->SetInt("u_DepthTexture", 0);
            m_SmoothShader->SetInt("u_Horizontal", 0);
            m_SmoothShader->SetFloat2("u_ScreenSize", glm::vec2(fluidW, fluidH));
            m_SmoothShader->SetFloat("u_FilterRadius", emitter.SmoothFilterRadius);
            m_SmoothShader->SetFloat("u_DepthFalloff", emitter.SmoothDepthFalloff);
            RenderFullscreenQuad();

            m_SmoothFBO[1]->Unbind();

            // Next iteration reads from the last output
            smoothInput = m_SmoothFBO[1]->GetColorAttachmentRendererID(0);
        }

        uint32_t smoothedDepthTex = smoothInput;

        // ================================================================
        // Pass 3: Thickness — additive blending
        // ================================================================
        m_ThicknessFBO->Bind();
        RenderCommand::SetViewport(0, 0, fluidW, fluidH);

        RenderCommand::SetClearColor({0.0f, 0.0f, 0.0f, 0.0f});
        RenderCommand::ClearColorOnly();

        RenderCommand::SetDepthTest(false);
        RenderCommand::SetDepthMask(false);
        RenderCommand::SetBlendFunc(BlendFactor::One, BlendFactor::One);

        particleBuffer->Bind(0);

        m_ThicknessShader->Bind();
        m_ThicknessShader->SetMat4("u_View", view);
        m_ThicknessShader->SetMat4("u_Projection", projection);
        m_ThicknessShader->SetFloat("u_ParticleRadius", particleRadius);

        emptyVAO->Bind();
        RenderCommand::DrawArraysInstanced(6, particleCount);

        // Restore blend and depth state
        RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha);
        RenderCommand::SetDepthMask(true);

        m_ThicknessFBO->Unbind();

        // ================================================================
        // Copy scene color to avoid feedback loop
        // Bind caller's FBO (HDR FBO) as read source, then copy its color attachment
        // ================================================================
        RenderCommand::BindFramebufferByID(callerFBO);
        RenderCommand::SetReadBuffer(0);
        RenderCommand::CopyFramebufferToTexture(m_SceneColorCopyTex->GetRendererID(), m_Width, m_Height);

        // ================================================================
        // Pass 4: Composite — final fluid surface rendering
        // Only write to color attachment 0 to avoid corrupting Entity ID (attachment 1)
        // ================================================================
        RenderCommand::SetDrawBuffer(0);

        RenderCommand::SetViewport(0, 0, m_Width, m_Height);
        RenderCommand::SetDepthTest(false);

        m_CompositeShader->Bind();

        // Bind textures
        RenderCommand::BindTextureUnit(0, smoothedDepthTex);
        m_CompositeShader->SetInt("u_FluidDepth", 0);

        RenderCommand::BindTextureUnit(1, m_ThicknessFBO->GetColorAttachmentRendererID(0));
        m_CompositeShader->SetInt("u_FluidThickness", 1);

        m_SceneColorCopyTex->Bind(2);
        m_CompositeShader->SetInt("u_SceneColor", 2);

        RenderCommand::BindTextureUnit(3, sceneDepthTexID);
        m_CompositeShader->SetInt("u_SceneDepth", 3);

        // Uniforms
        m_CompositeShader->SetMat4("u_InvProjection", invProjection);
        // u_ScreenSize 用于法线重建的邻域采样，必须匹配深度纹理的实际分辨率
        m_CompositeShader->SetFloat2("u_ScreenSize", glm::vec2(fluidW, fluidH));

        m_CompositeShader->SetFloat3("u_FluidColor", emitter.FluidColor);
        m_CompositeShader->SetFloat3("u_AbsorptionColor", emitter.AbsorptionColor);
        m_CompositeShader->SetFloat("u_AbsorptionScale", emitter.AbsorptionScale);
        m_CompositeShader->SetFloat("u_FresnelPower", emitter.FresnelPower);
        m_CompositeShader->SetFloat("u_RefractionStrength", emitter.RefractionStrength);
        m_CompositeShader->SetFloat("u_Reflectivity", emitter.Reflectivity);
        m_CompositeShader->SetFloat("u_RefractiveIndex", 1.333f);

        RenderFullscreenQuad();

        // Restore draw buffers for HDR FBO (attachment 0 + attachment 1)
        uint32_t drawBuffers[] = {0, 1};
        RenderCommand::SetDrawBuffers(2, drawBuffers);

        // Restore all GL state
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetClearColor(savedClearColor);
    }

#ifdef ENGINE_ENABLE_VULKAN

    // 拷贝调用者 HDR FBO 的 color0/depth 到独立 image（sceneColor 反馈环规避 + sceneDepth
    // layout 冲突规避）。必须在无活跃 render pass 时调用。
    void FluidRenderer::CopySceneAttachmentsVulkan(const Ref<Framebuffer>& hdrTarget)
    {
        auto*           ctx         = VulkanContext::Get();
        VkCommandBuffer cmd         = ctx ? ctx->GetCurrentFrameCommandBuffer() : VK_NULL_HANDLE;
        auto*           hdrFB       = static_cast<VulkanFramebuffer*>(hdrTarget.get());
        auto*           copyTex     = static_cast<VulkanTexture2D*>(m_SceneColorCopyTex.get());
        auto*           depthCopyFB = static_cast<VulkanFramebuffer*>(m_SceneDepthCopyFBO.get());
        if (cmd == VK_NULL_HANDLE || !hdrFB || !copyTex || !depthCopyFB)
            return;

        VulkanCommandBuffer cmdBuf(cmd);

        // ---- color0: SHADER_READ_ONLY -> TRANSFER_SRC -> copy -> SHADER_READ_ONLY ----
        const VkImage    srcColor = hdrFB->GetColorAttachmentImage(0);
        const VkImage    dstColor = copyTex->GetImage();
        const VkExtent3D extent{m_Width, m_Height, 1};
        if (srcColor != VK_NULL_HANDLE && dstColor != VK_NULL_HANDLE)
        {
            cmdBuf.ImageBarrier(srcColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT);
            cmdBuf.ImageBarrier(dstColor, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

            VkImageCopy region{};
            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.extent         = extent;
            vkCmdCopyImage(cmd, srcColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstColor,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            cmdBuf.ImageBarrier(srcColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            cmdBuf.ImageBarrier(dstColor, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        }

        // ---- depth: DEPTH_STENCIL_READ_ONLY -> TRANSFER_SRC -> copy -> DEPTH_STENCIL_READ_ONLY ----
        const VkImage srcDepth            = hdrFB->GetDepthAttachmentImage();
        const VkImage dstDepth            = depthCopyFB->GetDepthAttachmentImage();
        static bool   s_WarnedDepthFormat = false;
        if (hdrFB->GetDepthAttachmentFormat() != depthCopyFB->GetDepthAttachmentFormat())
        {
            if (!s_WarnedDepthFormat)
            {
                s_WarnedDepthFormat = true;
                ENGINE_CORE_WARN("[Fluid][Vulkan] depth copy FBO format mismatch, skip sceneDepth copy");
            }
        }
        else if (srcDepth != VK_NULL_HANDLE && dstDepth != VK_NULL_HANDLE)
        {
            // D24S8 未启用 separateDepthStencilLayouts 时 barrier/copy 必须带双 aspect
            constexpr VkImageAspectFlags kDepthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            cmdBuf.ImageBarrier(srcDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                kDepthAspect);
            cmdBuf.ImageBarrier(dstDepth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                VK_ACCESS_TRANSFER_WRITE_BIT, kDepthAspect);

            VkImageCopy region{};
            region.srcSubresource = {kDepthAspect, 0, 0, 1};
            region.dstSubresource = {kDepthAspect, 0, 0, 1};
            region.extent         = extent;
            vkCmdCopyImage(cmd, srcDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstDepth,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            cmdBuf.ImageBarrier(srcDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                VK_ACCESS_SHADER_READ_BIT, kDepthAspect);
            cmdBuf.ImageBarrier(dstDepth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT, kDepthAspect);
        }
    }

    void FluidRenderer::RenderVulkan(const Ref<ShaderStorageBuffer>& particleBuffer,
                                     const Ref<VertexArray>&         emptyVAO,
                                     uint32_t                        particleCount,
                                     float                           particleRadius,
                                     const glm::mat4&                view,
                                     const glm::mat4&                projection,
                                     const FluidEmitterComponent&    emitter,
                                     const Ref<Framebuffer>&         hdrTarget)
    {
        auto*                 ctx = VulkanContext::Get();
        const VkCommandBuffer cmd = ctx ? ctx->GetCurrentFrameCommandBuffer() : VK_NULL_HANDLE;
        if (!ctx || cmd == VK_NULL_HANDLE || !hdrTarget)
            return;

        // caller（SceneRenderer::RenderPipeline）的 HDR render pass 活跃中——Vulkan
        // 禁止嵌套 BeginRenderPass，先结束，composite 前重新 Bind（loadOp=LOAD 保内容）
        if (ctx->GetActiveSceneRenderPass() != VK_NULL_HANDLE)
        {
            VulkanCommandBuffer(cmd).EndRenderPass();
            ctx->SetActiveSceneRenderPass(VK_NULL_HANDLE, 0, false, 0, 0);
        }

        glm::vec4 savedClearColor = RenderCommand::GetClearColor();
        particleBuffer->Bind(0); // SSBO 槽 0（dispatcher 录制 depth/thickness draw 时消费）

        // ---- std140 UBO 打包（depth/thickness VS 共用）----
        FluidVSParamsUBO vsUbo{};
        vsUbo.View                 = view;
        vsUbo.Projection           = projection;
        vsUbo.ParticleRadiusAndPad = glm::vec4(particleRadius, 0.0f, 0.0f, 0.0f);
        m_FluidVSUBO->SetData(&vsUbo, sizeof(vsUbo));
        m_FluidVSUBO->Bind(1);

        // 可配置分辨率：Depth/Smooth/Thickness 在缩放分辨率下执行
        float    renderScale = std::clamp(emitter.RenderScale, 0.25f, 1.0f);
        uint32_t fluidW      = std::max(1u, static_cast<uint32_t>(m_Width * renderScale));
        uint32_t fluidH      = std::max(1u, static_cast<uint32_t>(m_Height * renderScale));

        m_DepthFBO->Resize(fluidW, fluidH);
        m_SmoothFBO[0]->Resize(fluidW, fluidH);
        m_SmoothFBO[1]->Resize(fluidW, fluidH);
        m_ThicknessFBO->Resize(fluidW, fluidH);
        if (m_SceneDepthCopyFBO)
            m_SceneDepthCopyFBO->Resize(m_Width, m_Height);

        // ================================================================
        // Pass 1: Depth — sphere impostor rendering to R32F
        // ================================================================
        m_DepthFBO->Bind();
        RenderCommand::SetViewport(0, 0, fluidW, fluidH);

        RenderCommand::SetClearColor({1e10f, 0.0f, 0.0f, 0.0f});
        RenderCommand::Clear();

        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthFunc(DepthFunc::Less);
        RenderCommand::SetDepthMask(true);

        // 与 GL 同因：防上游 SRC_ALPHA blend 把 R32F 输出乘 0（Vulkan pipeline 默认
        // blend 关闭，但保持状态一致以便跨后端行为对齐）
        bool prevBlend = RenderCommand::GetBlendEnabled();
        if (prevBlend)
            RenderCommand::SetBlend(false);

        m_DepthShader->Bind(); // u_View/u_Projection/u_ParticleRadius 经 FluidVSParamsUBO(binding 1)
        emptyVAO->Bind();
        RenderCommand::DrawArraysInstanced(6, particleCount);

        if (prevBlend)
            RenderCommand::SetBlend(true);

        m_DepthFBO->Unbind();

        // ================================================================
        // Pass 2: Bilateral smooth — ping-pong blur
        // ================================================================
        const Ref<Framebuffer> smoothInputFB    = m_DepthFBO;
        const int              smoothIterations = std::max(emitter.SmoothIterations, 1);

        RenderCommand::SetBlend(false);
        RenderCommand::SetScissorTest(false);
        RenderCommand::SetColorMask(true, true, true, true);

        auto* srcFB = static_cast<VulkanFramebuffer*>(smoothInputFB.get());
        for (int i = 0; i < smoothIterations; i++)
        {
            // Horizontal
            m_SmoothFBO[0]->Bind();
            RenderCommand::SetViewport(0, 0, fluidW, fluidH);
            RenderCommand::SetDepthTest(false);

            m_SmoothShader->Bind();
            RenderCommand::BindTextureView(0, srcFB->GetColorAttachmentViewHandle(0), nullptr);
            {
                FluidSmoothUBO sm{};
                sm.ScreenSizeAndParams = glm::vec4(static_cast<float>(fluidW), static_cast<float>(fluidH),
                                                   emitter.SmoothFilterRadius, emitter.SmoothDepthFalloff);
                sm.HorizontalAndPad    = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                m_SmoothParamsUBO->SetData(&sm, sizeof(sm));
                m_SmoothParamsUBO->Bind(1);
            }
            RenderFullscreenQuad();
            m_SmoothFBO[0]->Unbind();

            // Vertical
            m_SmoothFBO[1]->Bind();
            RenderCommand::SetViewport(0, 0, fluidW, fluidH);

            m_SmoothShader->Bind();
            RenderCommand::BindTextureView(0, m_SmoothFBO[0]->GetColorAttachmentViewHandle(0), nullptr);
            {
                FluidSmoothUBO sm{};
                sm.ScreenSizeAndParams = glm::vec4(static_cast<float>(fluidW), static_cast<float>(fluidH),
                                                   emitter.SmoothFilterRadius, emitter.SmoothDepthFalloff);
                sm.HorizontalAndPad    = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
                m_SmoothParamsUBO->SetData(&sm, sizeof(sm));
                m_SmoothParamsUBO->Bind(1);
            }
            RenderFullscreenQuad();
            m_SmoothFBO[1]->Unbind();

            srcFB = static_cast<VulkanFramebuffer*>(m_SmoothFBO[1].get());
        }

        // ================================================================
        // Pass 3: Thickness — additive blending
        // ================================================================
        m_ThicknessFBO->Bind();
        RenderCommand::SetViewport(0, 0, fluidW, fluidH);

        RenderCommand::SetClearColor({0.0f, 0.0f, 0.0f, 0.0f});
        RenderCommand::ClearColorOnly();

        RenderCommand::SetDepthTest(false);
        RenderCommand::SetDepthMask(false);
        RenderCommand::SetBlendFunc(BlendFactor::One, BlendFactor::One); // → pipeline additive blend

        particleBuffer->Bind(0);

        m_ThicknessShader->Bind(); // u_View/u_Projection/u_ParticleRadius 经 FluidVSParamsUBO(binding 1)
        emptyVAO->Bind();
        RenderCommand::DrawArraysInstanced(6, particleCount);

        RenderCommand::SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha);
        RenderCommand::SetDepthMask(true);

        m_ThicknessFBO->Unbind();

        // ================================================================
        // sceneColor / sceneDepth 拷贝（无活跃 render pass 时执行）
        // ================================================================
        CopySceneAttachmentsVulkan(hdrTarget);

        // ================================================================
        // Pass 4: Composite — 重新 Bind HDR（新 render pass，loadOp=LOAD）后合成
        // ================================================================
        hdrTarget->Bind();

        RenderCommand::SetDrawBuffer(0); // 只写 attachment 0（pipeline 对 attachment 1 关写）
        RenderCommand::SetViewport(0, 0, m_Width, m_Height);
        RenderCommand::SetDepthTest(false);

        m_CompositeShader->Bind();

        auto* depthCopyFB = static_cast<VulkanFramebuffer*>(m_SceneDepthCopyFBO.get());
        auto* copyTex     = static_cast<VulkanTexture2D*>(m_SceneColorCopyTex.get());
        RenderCommand::BindTextureView(0, srcFB->GetColorAttachmentViewHandle(0), nullptr);          // u_FluidDepth
        RenderCommand::BindTextureView(2, m_ThicknessFBO->GetColorAttachmentViewHandle(0), nullptr); // u_FluidThickness
        RenderCommand::BindTextureView(3, copyTex->GetImageView(), nullptr);                         // u_SceneColor
        RenderCommand::BindTextureView(10, depthCopyFB->GetDepthAttachmentSampledViewHandle(),
                                       nullptr); // u_SceneDepth（depth-only aspect 采样 view）

        {
            FluidCompositeParamsUBO comp{};
            comp.InvProjection   = glm::inverse(projection);
            comp.InvView         = glm::inverse(view);
            comp.FluidColor      = glm::vec4(emitter.FluidColor, 1.0f);
            comp.AbsorptionColor = glm::vec4(emitter.AbsorptionColor, 1.0f);
            comp.ScreenSizeAndAbs =
                glm::vec4(static_cast<float>(fluidW), static_cast<float>(fluidH), emitter.AbsorptionScale, 0.0f);
            comp.SurfaceParams =
                glm::vec4(emitter.FresnelPower, emitter.RefractionStrength, emitter.Reflectivity, 1.333f);
            m_CompositeUBO->SetData(&comp, sizeof(comp));
            m_CompositeUBO->Bind(4);
        }

        RenderFullscreenQuad();

        // 恢复状态（HDR FBO 留给 caller Unbind）
        {
            static const uint32_t drawBuffers[2] = {0, 1};
            RenderCommand::SetDrawBuffers(2, drawBuffers);
        }
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetClearColor(savedClearColor);
    }
#endif // ENGINE_ENABLE_VULKAN

} // namespace Engine
