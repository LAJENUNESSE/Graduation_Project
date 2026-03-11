#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Scene/Systems/LightSystem.h"
#include "Scene/Systems/ShadowSystem.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace Engine
{

    class EditorCamera;

    class TerrainRenderSystem
    {
    public:
        void Init();
        void UpdateTerrainMeshes(entt::registry& reg);
        void Render(entt::registry& reg, const EditorCamera& camera, const LightEnvironment& lights,
                    const ShadowData& shadow, const ShadowSettings& shadowSettings);
        void RenderDepth(entt::registry& reg, const Ref<Shader>& depthShader);

    private:
        Ref<Shader> m_TerrainShader;
        Ref<Texture2D> m_WhiteTexture;

        struct TerrainCache
        {
            std::string HeightmapPath;
            float HeightScale = 0;
            float TerrainSize = 0;
            int LODLevels = 0;
        };
        std::unordered_map<uint32_t, TerrainCache> m_Cache;

        // Splatmap 纹理缓存
        struct SplatmapCache
        {
            std::string Path;
            Ref<Texture2D> Texture;
        };
        std::unordered_map<uint32_t, SplatmapCache> m_SplatmapCache;
    };

} // namespace Engine
