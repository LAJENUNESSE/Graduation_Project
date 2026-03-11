#pragma once

#include "Core/Base.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Shader.h"
#include "Renderer/VertexArray.h"

namespace Engine
{

    struct PostProcessingSettings
    {
        bool BloomEnabled = true;
        float BloomThreshold = 1.0f;
        float BloomStrength = 0.3f;
        int BloomIterations = 3;

        // Tone Mapping: 0 = Reinhard, 1 = ACES
        int ToneMappingMode = 1;

        bool GammaCorrection = true;
    };

    class PostProcessing
    {
    public:
        PostProcessing();
        ~PostProcessing() = default;

        void Init(uint32_t width, uint32_t height);
        void Resize(uint32_t width, uint32_t height);

        // Execute the full post-processing pipeline:
        // Input: HDR scene texture (color attachment from main FBO)
        // Output: Renders to the currently bound framebuffer (or screen)
        void Process(uint32_t hdrTextureID, const PostProcessingSettings& settings);

        PostProcessingSettings& GetSettings() { return m_Settings; }

    private:
        void CreateShaders();
        void CreateFullscreenQuad();
        void RenderFullscreenQuad();

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;

        // Fullscreen quad
        Ref<VertexArray> m_QuadVAO;

        // Shaders
        Ref<Shader> m_BrightnessExtractShader;
        Ref<Shader> m_GaussianBlurShader;
        Ref<Shader> m_ToneMappingShader;

        // Ping-pong FBOs for bloom blur
        Ref<Framebuffer> m_BrightnessFBO;
        Ref<Framebuffer> m_PingPongFBO[2];

        PostProcessingSettings m_Settings;
    };

} // namespace Engine
