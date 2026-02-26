#pragma once

#include "Core/Base.h"
#include "Core/Timestep.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/ParticleSystemGPU.h"
#include "Asset/AssetHandle.h"
#include "Scene/Systems/LightSystem.h"
#include "Scene/Systems/ShadowSystem.h"
#include "Scene/Systems/SkyboxSystem.h"
#include "Scene/Systems/TerrainRenderSystem.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace Engine
{

    class Scene;
    class EditorCamera;

    struct RenderContext
    {
        EditorCamera* Camera = nullptr;
        Scene* ActiveScene = nullptr;
        float DeltaTime = 0.0f;
    };

    struct RenderPassConfig
    {
        std::string Name;
        std::function<void(RenderContext&)> ExecuteFn;
        bool Enabled = true;
    };

    class SceneRenderer
    {
    public:
        void Init();
        void Shutdown();

        void BeginScene(const EditorCamera& camera, Scene* scene, float deltaTime);
        void Render();
        void EndScene();

        // 供 MSAA 重绘只走渲染（不重复收集光照/阴影）
        void RenderGeometryAndSkybox();
        // 供 MSAA 解析后单独绘制粒子，避免颜色被 blit 覆盖
        void RenderParticlePass();

        ShadowSystem& GetShadowSystem() { return m_ShadowSystem; }
        SkyboxSystem& GetSkyboxSystem() { return m_SkyboxSystem; }
        TerrainRenderSystem& GetTerrainSystem() { return m_TerrainSystem; }

        // 供 EditorLayer 精细控制 pass 执行
        std::vector<RenderPassConfig>& GetPassQueue() { return m_PassQueue; }
        RenderContext& GetContext() { return m_Context; }

    private:
        std::vector<RenderPassConfig> m_PassQueue;

        RenderContext m_Context;
        LightEnvironment m_LightEnv;
        ShadowData m_ShadowData;

        ShadowSystem m_ShadowSystem;
        SkyboxSystem m_SkyboxSystem;
        TerrainRenderSystem m_TerrainSystem;
        RenderQueue m_RenderQueue;

        Ref<Shader> m_PBRShader;
        Ref<Texture2D> m_WhiteTexture;
        AssetHandle m_WhiteTextureHandle;

        // Particle systems keyed by entity ID
        std::unordered_map<uint32_t, Ref<ParticleSystemGPU>> m_ParticleSystems;
        Scene* m_LastScene = nullptr;
    };

} // namespace Engine
