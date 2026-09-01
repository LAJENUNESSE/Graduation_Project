#pragma once

#include "Core/Base.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Shader.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/Texture.h"
#include "Renderer/VertexArray.h"

#include <glm/glm.hpp>

#ifdef ENGINE_ENABLE_VULKAN
#include "Renderer/UniformBuffer.h"
#endif

namespace Engine
{

    struct FluidEmitterComponent;

    class FluidRenderer
    {
    public:
        FluidRenderer()  = default;
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
        // sceneColorTexID/sceneDepthTexID: scene color/depth (GL texture id；Vulkan 下忽略，
        //   由 hdrTarget 的 attachment 取 image/view)
        // emitter: rendering parameters (FluidColor, Fresnel, etc.)
        // hdrTarget: 调用者的 HDR FBO。GL 下 composite 直接写当前绑定；Vulkan 下用于
        //   sceneColor/sceneDepth 拷贝源与 composite 前重新 Bind（render pass 不能嵌套）
        void Render(const Ref<ShaderStorageBuffer>& particleBuffer,
                    const Ref<VertexArray>&         emptyVAO,
                    uint32_t                        particleCount,
                    float                           particleRadius,
                    const glm::mat4&                view,
                    const glm::mat4&                projection,
                    uint32_t                        sceneColorTexID,
                    uint32_t                        sceneDepthTexID,
                    const FluidEmitterComponent&    emitter,
                    const Ref<Framebuffer>&         hdrTarget = nullptr);

    private:
        void CreateFullscreenQuad();
        void RenderFullscreenQuad();
#ifdef ENGINE_ENABLE_VULKAN
        void RenderVulkan(const Ref<ShaderStorageBuffer>& particleBuffer,
                          const Ref<VertexArray>&         emptyVAO,
                          uint32_t                        particleCount,
                          float                           particleRadius,
                          const glm::mat4&                view,
                          const glm::mat4&                projection,
                          const FluidEmitterComponent&    emitter,
                          const Ref<Framebuffer>&         hdrTarget);
        void CopySceneAttachmentsVulkan(const Ref<Framebuffer>& hdrTarget);
#endif

        uint32_t m_Width       = 0;
        uint32_t m_Height      = 0;
        bool     m_Initialized = false;

        // FBOs
        Ref<Framebuffer> m_DepthFBO;     // R32F + DEPTH24STENCIL8
        Ref<Framebuffer> m_SmoothFBO[2]; // R32F ping-pong
        Ref<Framebuffer> m_ThicknessFBO; // R16F

        // Scene color copy texture (avoids feedback loop)
        Ref<Texture2D> m_SceneColorCopyTex;

        // Fullscreen quad
        Ref<VertexArray> m_QuadVAO;

        // Shaders
        Ref<Shader> m_DepthShader;
        Ref<Shader> m_SmoothShader;
        Ref<Shader> m_ThicknessShader;
        Ref<Shader> m_CompositeShader;

#ifdef ENGINE_ENABLE_VULKAN
        // Vulkan 路径：shader 大块参数走 std140 UBO（与 GLSL Vulkan 分支的 uniform
        // 块逐字节对应；dispatcher 通用 UBO 槽在 draw 录制时写 descriptor）。
        // binding 与 shader 声明一致：VS 1 / smooth 1 / composite 4。
        Ref<UniformBuffer> m_FluidVSUBO; // 144B depth/thickness 顶点参数
        // 32B smooth 每迭代参数：H/V 各一份。VulkanUniformBuffer::SetData 是立即
        // memcpy，单缓冲会让同一命令缓冲里先录制的 H-draw 在 GPU 执行时读到
        // 后写入的 V 参数（全变竖向平滑）。
        Ref<UniformBuffer> m_SmoothParamsUBO[2];
        Ref<UniformBuffer> m_CompositeUBO; // 192B composite 参数
        // u_SceneDepth 拷贝容器（depth-only FBO；HDR depth 在 composite 的
        // render pass 内是 attachment layout，直接采样会 layout 冲突）
        Ref<Framebuffer> m_SceneDepthCopyFBO;
#endif
    };

} // namespace Engine
