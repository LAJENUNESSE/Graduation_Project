#pragma once

#include "Asset/AssetHandle.h"
#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/GPUAsyncReadback.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/Texture.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/VertexArray.h"
#include "Scene/Systems/LightSystem.h"
#include "Scene/Systems/ShadowSystem.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

#ifdef ENGINE_ENABLE_VULKAN
#include <vulkan/vulkan.h>

#include "Platform/Vulkan/VulkanDescriptor.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#endif

namespace Engine
{

    class EditorCamera;
    class SceneEntityIndex;
    class WorldTransformCache;

    class GrassRenderSystem
    {
    public:
        void Init();
        void Shutdown();
        void UpdateGrassData(entt::registry& reg, float totalTime);
        void Render(entt::registry&         reg,
                    const EditorCamera&     camera,
                    const LightEnvironment& lights,
                    const ShadowData&       shadow,
                    const ShadowSettings&   shadowSettings,
                    float                   totalTime,
                    const SceneEntityIndex& index,
                    WorldTransformCache*    cache = nullptr);

    private:
        void RebuildGrass(uint32_t                         eid,
                          struct TerrainComponent&         tc,
                          const struct TransformComponent& transform,
                          struct TerrainMeshData*          meshData);

#ifdef ENGINE_ENABLE_VULKAN
        // Vulkan compute 资源 lazy 初始化（首次 RebuildGrass 时建立）
        void EnsureVulkanComputeResources();
        // 销毁 Vulkan 资源
        void DestroyVulkanComputeResources();
#endif

        Ref<Shader> m_PlacementShader;
        Ref<Shader> m_RenderArgsShader;
        Ref<Shader> m_BillboardShader;

        Ref<Texture2D>   m_WhiteTexture;
        Ref<VertexArray> m_EmptyVAO;

        bool m_UseIndirectDraw = true;

        // GLSL UBO（binding=2）共享于两个 backend，存放 grass_placement.glsl 的参数
        Ref<UniformBuffer> m_ParamsUBO;

        struct GrassInstance
        {
            Ref<ShaderStorageBuffer> GrassBuffer;    // binding 0 - 草叶数据
            Ref<ShaderStorageBuffer> HeightBuffer;   // binding 1 - 高度图
            Ref<ShaderStorageBuffer> CounterBuffer;  // binding 3 - grassCount
            Ref<ShaderStorageBuffer> IndirectArgs;   // binding 4 - DrawArraysIndirectCommand
            uint32_t                 GrassCount = 0; // fallback 用

            // Async readback（仅 OpenGL 路径使用，Vulkan 走同步回读）
            Ref<GPUAsyncReadback> Readback;
        };

        struct GrassCache
        {
            bool        GrassEnabled      = false;
            float       GrassDensity      = 0;
            float       GrassHeight       = 0;
            float       GrassWidth        = 0;
            float       GrassWindStrength = 0;
            float       TerrainSize       = 0;
            float       HeightScale       = 0;
            std::string HeightmapPath;
            AssetHandle GrassTexture;
        };

        std::unordered_map<uint32_t, GrassInstance> m_Instances;
        std::unordered_map<uint32_t, GrassCache>    m_Cache;

#ifdef ENGINE_ENABLE_VULKAN
        // ---- Vulkan compute pipeline 资源（lazy 初始化，全 entity 共享）----
        bool                           m_VulkanInitialized = false;
        VkDevice                       m_VkDevice          = VK_NULL_HANDLE;
        Ref<VulkanDescriptorSetLayout> m_PlacementSetLayout;
        Ref<VulkanDescriptorSetLayout> m_RenderArgsSetLayout;
        Ref<VulkanDescriptorPool>      m_DescriptorPool;
        VulkanComputePipelineHandle    m_PlacementPipeline{};
        VulkanComputePipelineHandle    m_RenderArgsPipeline{};
#endif
    };

} // namespace Engine
