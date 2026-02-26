#include "engpch.h"
#include "Scene/Systems/TerrainRenderSystem.h"
#include "Scene/Components.h"
#include "Terrain/TerrainMeshGenerator.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"
#include "Renderer/EditorCamera.h"
#include "Asset/AssetManager.h"
#include "Core/Log.h"

namespace Engine
{

    void TerrainRenderSystem::Init()
    {
        m_TerrainShader = Shader::Create("assets/shaders/TerrainPBR.glsl");

        auto whiteHandle = AssetManager::Load<Texture2D>("builtin:white");
        m_WhiteTexture = AssetManager::GetRef<Texture2D>(whiteHandle);
    }

    void TerrainRenderSystem::UpdateTerrainMeshes(entt::registry& reg)
    {
        auto view = reg.view<TransformComponent, TerrainComponent>();
        for (auto entity : view)
        {
            auto& tc = view.get<TerrainComponent>(entity);
            uint32_t eid = static_cast<uint32_t>(entity);

            auto& cache = m_Cache[eid];
            bool needRebuild = tc.MeshDirty
                || cache.HeightmapPath != tc.HeightmapPath
                || cache.HeightScale != tc.HeightScale
                || cache.TerrainSize != tc.TerrainSize
                || cache.LODLevels != tc.LODLevels;

            if (needRebuild && !tc.HeightmapPath.empty())
            {
                // 释放旧数据
                if (tc.RuntimeMeshData)
                {
                    delete static_cast<TerrainMeshData*>(tc.RuntimeMeshData);
                    tc.RuntimeMeshData = nullptr;
                }

                auto meshData = TerrainMeshGenerator::Generate(
                    tc.HeightmapPath, tc.TerrainSize, tc.HeightScale, tc.LODLevels);

                tc.RuntimeMeshData = new TerrainMeshData(std::move(meshData));
                tc.MeshDirty = false;

                cache.HeightmapPath = tc.HeightmapPath;
                cache.HeightScale = tc.HeightScale;
                cache.TerrainSize = tc.TerrainSize;
                cache.LODLevels = tc.LODLevels;
            }
        }
    }

    void TerrainRenderSystem::Render(entt::registry& reg, const EditorCamera& camera,
                                     const LightEnvironment& lights, const ShadowData& shadow,
                                     const ShadowSettings& shadowSettings)
    {
        auto view = reg.view<TransformComponent, TerrainComponent>();
        bool anyTerrain = false;

        for (auto entity : view)
        {
            auto& tc = view.get<TerrainComponent>(entity);
            auto* meshData = static_cast<TerrainMeshData*>(tc.RuntimeMeshData);
            if (!meshData || meshData->LODs.empty())
                continue;

            if (!anyTerrain)
            {
                // 首次绑定着色器 + 设置通用 uniform
                anyTerrain = true;

                Renderer::BeginScene(camera.GetViewProjection());

                m_TerrainShader->Bind();
                m_TerrainShader->SetFloat3("u_ViewPos", camera.GetPosition());
                LightSystem::UploadToShader(m_TerrainShader, lights);

                m_TerrainShader->SetMat4("u_LightSpaceMatrix", shadow.LightSpaceMatrix);
                bool shadowActive = shadowSettings.Enabled && shadow.HasValidShadowCaster;
                m_TerrainShader->SetInt("u_ShadowEnabled", shadowActive ? 1 : 0);
                m_TerrainShader->SetFloat("u_ShadowBias", shadowSettings.Bias);
                RenderCommand::BindTextureUnit(1, shadow.ShadowMapTextureID);
                m_TerrainShader->SetInt("u_ShadowMap", 1);
            }

            auto& transform = view.get<TransformComponent>(entity);
            uint32_t eid = static_cast<uint32_t>(entity);

            // LOD 选择
            float dist = glm::distance(camera.GetPosition(), transform.Translation);
            int lod = 0;
            if (dist > tc.LODDistance2 && static_cast<int>(meshData->LODs.size()) >= 3) lod = 2;
            else if (dist > tc.LODDistance1 && static_cast<int>(meshData->LODs.size()) >= 2) lod = 1;

            // 设置变换
            m_TerrainShader->SetMat4("u_Transform", transform.GetTransform());
            m_TerrainShader->SetMat4("u_ViewProjection", camera.GetViewProjection());
            m_TerrainShader->SetInt("u_EntityID", static_cast<int>(eid));

            // 绑定 Splatmap (unit 6)
            auto& splatCache = m_SplatmapCache[eid];
            if (!tc.SplatmapPath.empty())
            {
                if (splatCache.Path != tc.SplatmapPath || !splatCache.Texture)
                {
                    splatCache.Path = tc.SplatmapPath;
                    splatCache.Texture = Texture2D::Create(tc.SplatmapPath);
                }
                splatCache.Texture->Bind(6);
            }
            else
            {
                m_WhiteTexture->Bind(6);
            }
            m_TerrainShader->SetInt("u_Splatmap", 6);

            // 绑定 4 层纹理
            const int albedoUnits[4] = {7, 8, 9, 10};
            const int normalUnits[4] = {11, 12, 13, 14};
            const char* albedoNames[4] = {"u_Layer0Albedo", "u_Layer1Albedo", "u_Layer2Albedo", "u_Layer3Albedo"};
            const char* normalNames[4] = {"u_Layer0Normal", "u_Layer1Normal", "u_Layer2Normal", "u_Layer3Normal"};
            const char* hasNormalNames[4] = {"u_HasLayer0Normal", "u_HasLayer1Normal", "u_HasLayer2Normal", "u_HasLayer3Normal"};

            float tilings[4], metallics[4], roughnesses[4];
            for (int i = 0; i < 4; i++)
            {
                tilings[i] = tc.LayerTiling[i];
                metallics[i] = tc.LayerMetallic[i];
                roughnesses[i] = tc.LayerRoughness[i];

                // Albedo
                Texture2D* albedoTex = AssetManager::Get<Texture2D>(tc.LayerTextures[i]);
                if (albedoTex)
                    albedoTex->Bind(albedoUnits[i]);
                else
                    m_WhiteTexture->Bind(albedoUnits[i]);
                m_TerrainShader->SetInt(albedoNames[i], albedoUnits[i]);

                // Normal
                Texture2D* normalTex = AssetManager::Get<Texture2D>(tc.LayerNormalMaps[i]);
                if (normalTex)
                {
                    normalTex->Bind(normalUnits[i]);
                    m_TerrainShader->SetInt(normalNames[i], normalUnits[i]);
                    m_TerrainShader->SetInt(hasNormalNames[i], 1);
                }
                else
                {
                    m_WhiteTexture->Bind(normalUnits[i]);
                    m_TerrainShader->SetInt(normalNames[i], normalUnits[i]);
                    m_TerrainShader->SetInt(hasNormalNames[i], 0);
                }
            }

            // 上传数组 uniform
            for (int i = 0; i < 4; i++)
            {
                std::string tilingName = "u_LayerTiling[" + std::to_string(i) + "]";
                std::string metallicName = "u_LayerMetallic[" + std::to_string(i) + "]";
                std::string roughnessName = "u_LayerRoughness[" + std::to_string(i) + "]";
                m_TerrainShader->SetFloat(tilingName, tilings[i]);
                m_TerrainShader->SetFloat(metallicName, metallics[i]);
                m_TerrainShader->SetFloat(roughnessName, roughnesses[i]);
            }

            // 绘制
            auto& lodMesh = meshData->LODs[lod];
            lodMesh.VAO->Bind();
            RenderCommand::DrawIndexed(lodMesh.VAO);
        }

        if (anyTerrain)
            Renderer::EndScene();
    }

    void TerrainRenderSystem::RenderDepth(entt::registry& reg, const Ref<Shader>& depthShader)
    {
        auto view = reg.view<TransformComponent, TerrainComponent>();
        for (auto entity : view)
        {
            auto& tc = view.get<TerrainComponent>(entity);
            auto* meshData = static_cast<TerrainMeshData*>(tc.RuntimeMeshData);
            if (!meshData || meshData->LODs.empty())
                continue;

            auto& transform = view.get<TransformComponent>(entity);
            depthShader->SetMat4("u_Transform", transform.GetTransform());

            // 阴影用 LOD0（最高精度）
            meshData->LODs[0].VAO->Bind();
            RenderCommand::DrawIndexed(meshData->LODs[0].VAO);
        }
    }

} // namespace Engine
