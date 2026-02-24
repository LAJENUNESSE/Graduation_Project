#pragma once

#include "Core/Base.h"
#include "Core/Timestep.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Renderer/RenderQueue.h"
#include "Scene/Systems/LightSystem.h"
#include "Scene/Systems/ShadowSystem.h"
#include "Scene/Systems/SkyboxSystem.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>

namespace Engine
{

    class Scene;
    class EditorCamera;

    struct RenderContext
    {
        EditorCamera* Camera = nullptr;
        Scene* ActiveScene = nullptr;
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

        void BeginScene(const EditorCamera& camera, Scene* scene);
        void Render();
        void EndScene();

        // 供 MSAA 重绘只走渲染（不重复收集光照/阴影）
        void RenderGeometryAndSkybox();

        ShadowSystem& GetShadowSystem() { return m_ShadowSystem; }
        SkyboxSystem& GetSkyboxSystem() { return m_SkyboxSystem; }

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
        RenderQueue m_RenderQueue;

        Ref<Shader> m_PBRShader;
        Ref<Texture2D> m_WhiteTexture;
    };

} // namespace Engine
