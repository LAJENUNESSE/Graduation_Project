#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/VertexArray.h"
#include "Asset/AssetHandle.h"
#include "Scene/Systems/LightSystem.h"
#include "Scene/Systems/ShadowSystem.h"

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <unordered_map>
#include <string>

namespace Engine
{

    class EditorCamera;

    class GrassRenderSystem
    {
    public:
        void Init();
        void Shutdown();
        void UpdateGrassData(entt::registry& reg, float totalTime);
        void Render(entt::registry& reg, const EditorCamera& camera,
                    const LightEnvironment& lights, const ShadowData& shadow,
                    const ShadowSettings& shadowSettings, float totalTime);

    private:
        void RebuildGrass(uint32_t eid, struct TerrainComponent& tc,
                          const struct TransformComponent& transform);

        Ref<Shader> m_PlacementShader;
        Ref<Shader> m_RenderArgsShader;
        Ref<Shader> m_BillboardShader;

        Ref<Texture2D> m_WhiteTexture;
        Ref<VertexArray> m_EmptyVAO;

        bool m_UseIndirectDraw = true;

        struct GrassInstance
        {
            Ref<ShaderStorageBuffer> GrassBuffer;    // binding 0 - 草叶数据
            Ref<ShaderStorageBuffer> HeightBuffer;   // binding 1 - 高度图
            Ref<ShaderStorageBuffer> CounterBuffer;  // binding 3 - grassCount
            Ref<ShaderStorageBuffer> IndirectArgs;   // binding 4 - DrawArraysIndirectCommand
            uint32_t GrassCount = 0;                 // fallback 用

            // 异步回读（避免 glGetBufferSubData 同步阻塞）
            uint32_t ReadbackBuffer = 0;    // PBO，用于异步拷贝
            void*    ReadbackFence = nullptr; // GLsync 栅栏
            bool     ReadbackPending = false;
        };

        struct GrassCache
        {
            bool GrassEnabled = false;
            float GrassDensity = 0;
            float GrassHeight = 0;
            float GrassWidth = 0;
            float GrassWindStrength = 0;
            float TerrainSize = 0;
            float HeightScale = 0;
            std::string HeightmapPath;
            AssetHandle GrassTexture;
        };

        std::unordered_map<uint32_t, GrassInstance> m_Instances;
        std::unordered_map<uint32_t, GrassCache> m_Cache;
    };

} // namespace Engine
