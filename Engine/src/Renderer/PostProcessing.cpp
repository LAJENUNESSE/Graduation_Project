#include "engpch.h"
#include "Renderer/PostProcessing.h"
#include "Renderer/Buffer.h"
#include "Renderer/RenderCommand.h"

namespace Engine
{

    PostProcessing::PostProcessing() {}

    void PostProcessing::CreateFullscreenQuad()
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
        auto vb = VertexBuffer::Create(quadVertices, sizeof(quadVertices));
        vb->SetLayout({
            {ShaderDataType::Float2, "a_Position"},
            {ShaderDataType::Float2, "a_TexCoord"},
        });
        m_QuadVAO->AddVertexBuffer(vb);

        auto ib = IndexBuffer::Create(quadIndices, 6);
        m_QuadVAO->SetIndexBuffer(ib);
    }

    void PostProcessing::CreateShaders()
    {
        m_BrightnessExtractShader = Shader::Create("assets/shaders/BrightnessExtract.glsl");
        m_GaussianBlurShader = Shader::Create("assets/shaders/GaussianBlur.glsl");
        m_ToneMappingShader = Shader::Create("assets/shaders/ToneMapping.glsl");
    }

    void PostProcessing::Init(uint32_t width, uint32_t height)
    {
        if (!m_QuadVAO)
        {
            CreateShaders();
            CreateFullscreenQuad();
        }

        m_Width = width;
        m_Height = height;

        uint32_t halfW = std::max(width / 2, 1u);
        uint32_t halfH = std::max(height / 2, 1u);

        // Brightness extraction FBO (half-res RGBA16F, no depth)
        {
            FramebufferSpecification spec;
            spec.Attachments = {FramebufferTextureFormat::RGBA16F};
            spec.Width = halfW;
            spec.Height = halfH;
            m_BrightnessFBO = Framebuffer::Create(spec);
        }

        // Ping-pong FBOs for Gaussian blur (half-res RGBA16F, no depth)
        for (int i = 0; i < 2; i++)
        {
            FramebufferSpecification spec;
            spec.Attachments = {FramebufferTextureFormat::RGBA16F};
            spec.Width = halfW;
            spec.Height = halfH;
            m_PingPongFBO[i] = Framebuffer::Create(spec);
        }
    }

    void PostProcessing::Resize(uint32_t width, uint32_t height)
    {
        if (width == m_Width && height == m_Height)
            return;
        if (width == 0 || height == 0)
            return;

        m_Width = width;
        m_Height = height;

        uint32_t halfW = std::max(width / 2, 1u);
        uint32_t halfH = std::max(height / 2, 1u);

        m_BrightnessFBO->Resize(halfW, halfH);
        m_PingPongFBO[0]->Resize(halfW, halfH);
        m_PingPongFBO[1]->Resize(halfW, halfH);
    }

    void PostProcessing::RenderFullscreenQuad()
    {
        m_QuadVAO->Bind();
        RenderCommand::DrawIndexed(m_QuadVAO);
    }

    void PostProcessing::Process(uint32_t hdrTextureID, const PostProcessingSettings& settings)
    {
        // Save caller's FBO so we can restore it for the final tone mapping pass
        int callerFBO = RenderCommand::GetBoundFramebufferID();

        uint32_t bloomTextureID = 0;

        if (settings.BloomEnabled)
        {
            // Step 1: Brightness extraction
            m_BrightnessFBO->Bind();
            RenderCommand::ClearColorOnly();

            m_BrightnessExtractShader->Bind();
            RenderCommand::BindTextureUnit(0, hdrTextureID);
            m_BrightnessExtractShader->SetInt("u_HDRBuffer", 0);
            m_BrightnessExtractShader->SetFloat("u_Threshold", settings.BloomThreshold);
            RenderFullscreenQuad();

            // Step 2: Gaussian blur (ping-pong)
            bool horizontal = true;
            uint32_t inputTexture = m_BrightnessFBO->GetColorAttachmentRendererID(0);

            m_GaussianBlurShader->Bind();
            for (int i = 0; i < settings.BloomIterations * 2; i++)
            {
                m_PingPongFBO[horizontal ? 0 : 1]->Bind();
                RenderCommand::ClearColorOnly();

                m_GaussianBlurShader->SetInt("u_Horizontal", horizontal ? 1 : 0);
                RenderCommand::BindTextureUnit(0, inputTexture);
                m_GaussianBlurShader->SetInt("u_Image", 0);
                RenderFullscreenQuad();

                inputTexture = m_PingPongFBO[horizontal ? 0 : 1]->GetColorAttachmentRendererID(0);
                horizontal = !horizontal;
            }

            bloomTextureID = inputTexture;
        }

        // Step 3: Tone mapping + composite — restore caller's FBO
        RenderCommand::BindFramebufferByID(callerFBO);
        RenderCommand::SetViewport(0, 0, m_Width, m_Height);

        m_ToneMappingShader->Bind();

        RenderCommand::BindTextureUnit(0, hdrTextureID);
        m_ToneMappingShader->SetInt("u_HDRBuffer", 0);

        RenderCommand::BindTextureUnit(1, bloomTextureID ? bloomTextureID : 0);
        m_ToneMappingShader->SetInt("u_BloomBlur", 1);

        m_ToneMappingShader->SetFloat("u_BloomStrength", settings.BloomStrength);
        m_ToneMappingShader->SetInt("u_ToneMappingMode", settings.ToneMappingMode);
        m_ToneMappingShader->SetInt("u_GammaCorrection", settings.GammaCorrection ? 1 : 0);
        m_ToneMappingShader->SetInt("u_BloomEnabled", settings.BloomEnabled ? 1 : 0);

        RenderFullscreenQuad();
    }

} // namespace Engine
