#include "engpch.h"
#include "Renderer/PostProcessing.h"
#include "Renderer/Buffer.h"
#include "Renderer/RenderCommand.h"

#include <glad/gl.h>

namespace Engine
{

    PostProcessing::PostProcessing()
    {
        CreateShaders();
        CreateFullscreenQuad();
    }

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
        // === Brightness Extract Shader ===
        std::string brightVertSrc = R"(
            #version 330 core
            layout(location = 0) in vec2 a_Position;
            layout(location = 1) in vec2 a_TexCoord;
            out vec2 v_TexCoord;
            void main() {
                v_TexCoord = a_TexCoord;
                gl_Position = vec4(a_Position, 0.0, 1.0);
            }
        )";

        std::string brightFragSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 FragColor;
            in vec2 v_TexCoord;

            uniform sampler2D u_HDRBuffer;
            uniform float u_Threshold;

            void main() {
                vec3 color = texture(u_HDRBuffer, v_TexCoord).rgb;
                float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
                FragColor = (brightness > u_Threshold) ? vec4(color, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
            }
        )";
        m_BrightnessExtractShader = Shader::Create("BrightnessExtract", brightVertSrc, brightFragSrc);

        // === Gaussian Blur Shader (separable) ===
        std::string blurVertSrc = brightVertSrc; // Same vertex shader

        std::string blurFragSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 FragColor;
            in vec2 v_TexCoord;

            uniform sampler2D u_Image;
            uniform int u_Horizontal;

            // 5-tap Gaussian weights
            const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

            void main() {
                vec2 texOffset = 1.0 / textureSize(u_Image, 0);
                vec3 result = texture(u_Image, v_TexCoord).rgb * weight[0];

                if (u_Horizontal != 0) {
                    for (int i = 1; i < 5; ++i) {
                        result += texture(u_Image, v_TexCoord + vec2(texOffset.x * float(i), 0.0)).rgb * weight[i];
                        result += texture(u_Image, v_TexCoord - vec2(texOffset.x * float(i), 0.0)).rgb * weight[i];
                    }
                } else {
                    for (int i = 1; i < 5; ++i) {
                        result += texture(u_Image, v_TexCoord + vec2(0.0, texOffset.y * float(i))).rgb * weight[i];
                        result += texture(u_Image, v_TexCoord - vec2(0.0, texOffset.y * float(i))).rgb * weight[i];
                    }
                }
                FragColor = vec4(result, 1.0);
            }
        )";
        m_GaussianBlurShader = Shader::Create("GaussianBlur", blurVertSrc, blurFragSrc);

        // === Tone Mapping + Composite Shader ===
        std::string toneVertSrc = brightVertSrc; // Same vertex shader

        std::string toneFragSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 FragColor;
            in vec2 v_TexCoord;

            uniform sampler2D u_HDRBuffer;
            uniform sampler2D u_BloomBlur;
            uniform float u_BloomStrength;
            uniform int u_ToneMappingMode;
            uniform int u_GammaCorrection;
            uniform int u_BloomEnabled;

            // ACES Filmic Tone Mapping
            vec3 ACESFilm(vec3 x) {
                float a = 2.51;
                float b = 0.03;
                float c = 2.43;
                float d = 0.59;
                float e = 0.14;
                return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
            }

            void main() {
                vec3 hdrColor = texture(u_HDRBuffer, v_TexCoord).rgb;

                // Add bloom
                if (u_BloomEnabled != 0) {
                    vec3 bloomColor = texture(u_BloomBlur, v_TexCoord).rgb;
                    hdrColor += bloomColor * u_BloomStrength;
                }

                // Tone mapping
                vec3 mapped;
                if (u_ToneMappingMode == 0) {
                    // Reinhard
                    mapped = hdrColor / (hdrColor + vec3(1.0));
                } else {
                    // ACES
                    mapped = ACESFilm(hdrColor);
                }

                // Gamma correction
                if (u_GammaCorrection != 0)
                    mapped = pow(mapped, vec3(1.0 / 2.2));

                FragColor = vec4(mapped, 1.0);
            }
        )";
        m_ToneMappingShader = Shader::Create("ToneMapping", toneVertSrc, toneFragSrc);
    }

    void PostProcessing::Init(uint32_t width, uint32_t height)
    {
        m_Width = width;
        m_Height = height;

        // Brightness extraction FBO (RGBA16F, no depth)
        {
            FramebufferSpecification spec;
            spec.Attachments = {FramebufferTextureFormat::RGBA16F};
            spec.Width = width;
            spec.Height = height;
            m_BrightnessFBO = Framebuffer::Create(spec);
        }

        // Ping-pong FBOs for Gaussian blur (RGBA16F, no depth)
        for (int i = 0; i < 2; i++)
        {
            FramebufferSpecification spec;
            spec.Attachments = {FramebufferTextureFormat::RGBA16F};
            spec.Width = width;
            spec.Height = height;
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

        m_BrightnessFBO->Resize(width, height);
        m_PingPongFBO[0]->Resize(width, height);
        m_PingPongFBO[1]->Resize(width, height);
    }

    void PostProcessing::RenderFullscreenQuad()
    {
        m_QuadVAO->Bind();
        RenderCommand::DrawIndexed(m_QuadVAO);
    }

    void PostProcessing::Process(uint32_t hdrTextureID, const PostProcessingSettings& settings)
    {
        // Save caller's FBO so we can restore it for the final tone mapping pass
        GLint callerFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &callerFBO);

        uint32_t bloomTextureID = 0;

        if (settings.BloomEnabled)
        {
            // Step 1: Brightness extraction
            m_BrightnessFBO->Bind();
            glClear(GL_COLOR_BUFFER_BIT);

            m_BrightnessExtractShader->Bind();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, hdrTextureID);
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
                glClear(GL_COLOR_BUFFER_BIT);

                m_GaussianBlurShader->SetInt("u_Horizontal", horizontal ? 1 : 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, inputTexture);
                m_GaussianBlurShader->SetInt("u_Image", 0);
                RenderFullscreenQuad();

                inputTexture = m_PingPongFBO[horizontal ? 0 : 1]->GetColorAttachmentRendererID(0);
                horizontal = !horizontal;
            }

            bloomTextureID = inputTexture;
        }

        // Step 3: Tone mapping + composite — restore caller's FBO
        glBindFramebuffer(GL_FRAMEBUFFER, callerFBO);
        glViewport(0, 0, m_Width, m_Height);

        m_ToneMappingShader->Bind();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrTextureID);
        m_ToneMappingShader->SetInt("u_HDRBuffer", 0);

        glActiveTexture(GL_TEXTURE1);
        if (bloomTextureID)
            glBindTexture(GL_TEXTURE_2D, bloomTextureID);
        m_ToneMappingShader->SetInt("u_BloomBlur", 1);

        m_ToneMappingShader->SetFloat("u_BloomStrength", settings.BloomStrength);
        m_ToneMappingShader->SetInt("u_ToneMappingMode", settings.ToneMappingMode);
        m_ToneMappingShader->SetInt("u_GammaCorrection", settings.GammaCorrection ? 1 : 0);
        m_ToneMappingShader->SetInt("u_BloomEnabled", settings.BloomEnabled ? 1 : 0);

        RenderFullscreenQuad();
    }

} // namespace Engine
