#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/VertexArray.h"
#include "Renderer/StorageBuffer.h"

#include <glm/glm.hpp>

namespace Engine
{

    struct FluidEmitterComponent;

    class FluidRenderer
    {
    public:
        FluidRenderer() = default;
        ~FluidRenderer() = default;

        void Init(uint32_t width, uint32_t height);
        void Resize(uint32_t width, uint32_t height);
        void Shutdown();

        // Render fluid surface using Screen-Space Fluid Rendering
        // particleBuffer: SSBO containing FluidGPUParticle data (binding 0)
        // emptyVAO: VAO for instanced draw
        // particleCount: number of alive particles
        // particleRadius: radius for sphere impostors
        // view, projection: camera matrices
        // sceneColorTexID: scene HDR color texture (read-only copy will be made)
        // sceneDepthTexID: scene depth texture
        // emitter: rendering parameters (FluidColor, Fresnel, etc.)
        void Render(const Ref<ShaderStorageBuffer>& particleBuffer,
                    const Ref<VertexArray>& emptyVAO,
                    uint32_t particleCount,
                    float particleRadius,
                    const glm::mat4& view,
                    const glm::mat4& projection,
                    uint32_t sceneColorTexID,
                    uint32_t sceneDepthTexID,
                    const FluidEmitterComponent& emitter);

    private:
        void CreateFullscreenQuad();
        void RenderFullscreenQuad();

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool m_Initialized = false;

        // FBOs
        Ref<Framebuffer> m_DepthFBO;       // R32F + DEPTH24STENCIL8
        Ref<Framebuffer> m_SmoothFBO[2];   // R32F ping-pong
        Ref<Framebuffer> m_ThicknessFBO;   // R16F

        // Scene color copy texture (avoids feedback loop)
        uint32_t m_SceneColorCopyTex = 0;
        uint32_t m_SceneColorCopyWidth = 0;
        uint32_t m_SceneColorCopyHeight = 0;

        // Fullscreen quad
        Ref<VertexArray> m_QuadVAO;

        // Shaders
        Ref<Shader> m_DepthShader;
        Ref<Shader> m_SmoothShader;
        Ref<Shader> m_ThicknessShader;
        Ref<Shader> m_CompositeShader;
    };

} // namespace Engine
