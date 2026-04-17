#include "engpch.h"
#include "Renderer/FluidRenderer.h"
#include "Core/Log.h"
#include "Renderer/Buffer.h"
#include "Renderer/RenderCommand.h"
#include "Scene/Components.h"

#include <glad/gl.h>

namespace Engine
{

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
        glGenTextures(1, &m_SceneColorCopyTex);
        glBindTexture(GL_TEXTURE_2D, m_SceneColorCopyTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_SceneColorCopyWidth  = width;
        m_SceneColorCopyHeight = height;

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

        m_DepthFBO->Resize(width, height);
        m_SmoothFBO[0]->Resize(width, height);
        m_SmoothFBO[1]->Resize(width, height);
        m_ThicknessFBO->Resize(width, height);

        // Resize scene color copy texture
        if (width != m_SceneColorCopyWidth || height != m_SceneColorCopyHeight)
        {
            glBindTexture(GL_TEXTURE_2D, m_SceneColorCopyTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
            glBindTexture(GL_TEXTURE_2D, 0);
            m_SceneColorCopyWidth  = width;
            m_SceneColorCopyHeight = height;
        }
    }

    void FluidRenderer::Shutdown()
    {
        if (m_SceneColorCopyTex)
        {
            glDeleteTextures(1, &m_SceneColorCopyTex);
            m_SceneColorCopyTex = 0;
        }
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
                               const FluidEmitterComponent&    emitter)
    {
        if (!m_Initialized || particleCount == 0)
            return;

        // Save caller's FBO and GL state
        int     callerFBO = RenderCommand::GetBoundFramebufferID();
        GLfloat savedClearColor[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, savedClearColor);

        // [DIAG] 首次运行时打印 FluidRenderer 的 FBO 尺寸和 callerFBO ID
        static bool s_FluidRenderLogged = false;
        if (!s_FluidRenderLogged)
        {
            s_FluidRenderLogged = true;
            ENGINE_WARN("[FluidRenderer][diag] first Render(): size={}x{} callerFBO={} particleCount={} "
                        "radius={:.4f} smoothIter={}",
                        m_Width, m_Height, callerFBO, particleCount, particleRadius, emitter.SmoothIterations);
        }

        // Bind particle buffer for instanced draw
        particleBuffer->Bind(0);

        glm::mat4 invProjection = glm::inverse(projection);
        glm::mat4 invView       = glm::inverse(view);

        // ================================================================
        // Pass 1: Depth — sphere impostor rendering to R32F
        // ================================================================
        m_DepthFBO->Bind();
        RenderCommand::SetViewport(0, 0, m_Width, m_Height);

        // Clear depth FBO to far value (1e10)
        glClearColor(1e10f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthFunc(DepthFunc::Less);
        RenderCommand::SetDepthMask(true);

        m_DepthShader->Bind();
        m_DepthShader->SetMat4("u_View", view);
        m_DepthShader->SetMat4("u_Projection", projection);
        m_DepthShader->SetFloat("u_ParticleRadius", particleRadius);

        emptyVAO->Bind();
        RenderCommand::DrawArraysInstanced(6, particleCount);

        // [DIAG] 首次运行时扫描整张 R32F depth 纹理，统计 coverage/min/max
        // 并按 2x2 象限报告 bounding box，判断粒子云落在屏幕哪个区域
        static bool s_DepthReadbackLogged = false;
        if (!s_DepthReadbackLogged)
        {
            s_DepthReadbackLogged = true;
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            const int          W = static_cast<int>(m_Width);
            const int          H = static_cast<int>(m_Height);
            std::vector<float> buf(static_cast<size_t>(W) * H, 0.0f);
            glReadPixels(0, 0, W, H, GL_RED, GL_FLOAT, buf.data());

            uint32_t coverage = 0;
            float    minZ     = 1e10f;
            float    maxZ     = -1e10f;
            int      minX = W, minY = H, maxX = -1, maxY = -1;
            // 4 象限 coverage（像素计数）
            uint32_t q[4] = {0, 0, 0, 0}; // TL, TR, BL, BR
            for (int y = 0; y < H; ++y)
            {
                for (int x = 0; x < W; ++x)
                {
                    float v = buf[static_cast<size_t>(y) * W + x];
                    if (v < 1e9f)
                    {
                        ++coverage;
                        if (v < minZ)
                            minZ = v;
                        if (v > maxZ)
                            maxZ = v;
                        if (x < minX)
                            minX = x;
                        if (y < minY)
                            minY = y;
                        if (x > maxX)
                            maxX = x;
                        if (y > maxY)
                            maxY = y;
                        int qi = (x < W / 2 ? 0 : 1) + (y < H / 2 ? 0 : 2);
                        ++q[qi];
                    }
                }
            }
            float coveragePct = 100.0f * static_cast<float>(coverage) / (static_cast<float>(W) * H);
            ENGINE_WARN("[FluidRenderer][diag] depth scan (full {}x{}): coverage={} px ({:.3f}%) "
                        "viewZ range=[{:.3f},{:.3f}] bbox=({}..{}, {}..{}) "
                        "quadPx TL={} TR={} BL={} BR={}",
                        W, H, coverage, coveragePct, minZ, maxZ, minX, maxX, minY, maxY, q[0], q[1], q[2], q[3]);
        }

        m_DepthFBO->Unbind();

        // ================================================================
        // Pass 2: Bilateral smooth — ping-pong blur
        // ================================================================
        uint32_t smoothInput = m_DepthFBO->GetColorAttachmentRendererID(0);

        int smoothIterations = std::max(emitter.SmoothIterations, 1);
        for (int i = 0; i < smoothIterations; i++)
        {
            // Horizontal pass
            m_SmoothFBO[0]->Bind();
            RenderCommand::SetViewport(0, 0, m_Width, m_Height);
            RenderCommand::SetDepthTest(false);

            m_SmoothShader->Bind();
            RenderCommand::BindTextureUnit(0, smoothInput);
            m_SmoothShader->SetInt("u_DepthTexture", 0);
            m_SmoothShader->SetInt("u_Horizontal", 1);
            m_SmoothShader->SetFloat2("u_ScreenSize", glm::vec2(m_Width, m_Height));
            m_SmoothShader->SetFloat("u_FilterRadius", emitter.SmoothFilterRadius);
            m_SmoothShader->SetFloat("u_DepthFalloff", emitter.SmoothDepthFalloff);
            RenderFullscreenQuad();

            m_SmoothFBO[0]->Unbind();

            // Vertical pass
            m_SmoothFBO[1]->Bind();
            RenderCommand::SetViewport(0, 0, m_Width, m_Height);

            m_SmoothShader->Bind();
            RenderCommand::BindTextureUnit(0, m_SmoothFBO[0]->GetColorAttachmentRendererID(0));
            m_SmoothShader->SetInt("u_DepthTexture", 0);
            m_SmoothShader->SetInt("u_Horizontal", 0);
            m_SmoothShader->SetFloat2("u_ScreenSize", glm::vec2(m_Width, m_Height));
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
        RenderCommand::SetViewport(0, 0, m_Width, m_Height);

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

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
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindTexture(GL_TEXTURE_2D, m_SceneColorCopyTex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, m_Width, m_Height);
        glBindTexture(GL_TEXTURE_2D, 0);

        // ================================================================
        // Pass 4: Composite — final fluid surface rendering
        // Only write to color attachment 0 to avoid corrupting Entity ID (attachment 1)
        // ================================================================
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        RenderCommand::SetViewport(0, 0, m_Width, m_Height);
        RenderCommand::SetDepthTest(false);

        m_CompositeShader->Bind();

        // Bind textures
        RenderCommand::BindTextureUnit(0, smoothedDepthTex);
        m_CompositeShader->SetInt("u_FluidDepth", 0);

        RenderCommand::BindTextureUnit(1, m_ThicknessFBO->GetColorAttachmentRendererID(0));
        m_CompositeShader->SetInt("u_FluidThickness", 1);

        RenderCommand::BindTextureUnit(2, m_SceneColorCopyTex);
        m_CompositeShader->SetInt("u_SceneColor", 2);

        RenderCommand::BindTextureUnit(3, sceneDepthTexID);

        // Uniforms
        m_CompositeShader->SetMat4("u_InvProjection", invProjection);
        m_CompositeShader->SetFloat2("u_ScreenSize", glm::vec2(m_Width, m_Height));

        m_CompositeShader->SetFloat3("u_FluidColor", emitter.FluidColor);
        m_CompositeShader->SetFloat3("u_AbsorptionColor", emitter.AbsorptionColor);
        m_CompositeShader->SetFloat("u_AbsorptionScale", emitter.AbsorptionScale);
        m_CompositeShader->SetFloat("u_FresnelPower", emitter.FresnelPower);
        m_CompositeShader->SetFloat("u_RefractionStrength", emitter.RefractionStrength);
        m_CompositeShader->SetFloat("u_Reflectivity", emitter.Reflectivity);
        m_CompositeShader->SetFloat("u_RefractiveIndex", 1.333f);

        RenderFullscreenQuad();

        // [DIAG] 首次运行 Composite 后读回 HDR FBO attachment 0 中心像素 + 场景颜色 copy 中心像素
        // 若两者一致 → Composite 走了 early-return 分支；若不同 → Composite 实际写入了流体颜色
        static bool s_CompositeReadbackLogged = false;
        if (!s_CompositeReadbackLogged)
        {
            s_CompositeReadbackLogged = true;
            int   cx                  = static_cast<int>(m_Width) / 2;
            int   cy                  = static_cast<int>(m_Height) / 2;
            float hdrPixel[4]         = {0};
            float copyPixel[4]        = {0};

            // 当前绑定在 callerFBO (HDR)；读 attachment 0
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_FLOAT, hdrPixel);

            // 读 m_SceneColorCopyTex（通过临时 FBO 绑定）以对比
            GLuint tmpFBO = 0;
            glGenFramebuffers(1, &tmpFBO);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, tmpFBO);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_SceneColorCopyTex, 0);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_FLOAT, copyPixel);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &tmpFBO);

            // 恢复 callerFBO 绑定
            RenderCommand::BindFramebufferByID(callerFBO);
            glReadBuffer(GL_COLOR_ATTACHMENT0);

            float diff = std::abs(hdrPixel[0] - copyPixel[0]) + std::abs(hdrPixel[1] - copyPixel[1]) +
                         std::abs(hdrPixel[2] - copyPixel[2]);
            ENGINE_WARN("[FluidRenderer][diag] composite readback @ center ({}x{}): "
                        "hdrAfter=({:.4f},{:.4f},{:.4f},{:.4f}) "
                        "sceneCopy=({:.4f},{:.4f},{:.4f},{:.4f}) diff={:.4f} "
                        "(diff>0.001 → composite 写入了不同颜色)",
                        cx, cy, hdrPixel[0], hdrPixel[1], hdrPixel[2], hdrPixel[3], copyPixel[0], copyPixel[1],
                        copyPixel[2], copyPixel[3], diff);
        }

        // Restore draw buffers for HDR FBO (attachment 0 + attachment 1)
        GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, drawBuffers);

        // Restore all GL state
        RenderCommand::SetDepthTest(true);
        glClearColor(savedClearColor[0], savedClearColor[1], savedClearColor[2], savedClearColor[3]);
    }

} // namespace Engine
