#include "engpch.h"
#include "Scene/Systems/MeshRenderSystem.h"
#include "Scene/Components.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"
#include "Asset/AssetManager.h"

namespace Engine
{

    // 递归计算世界变换矩阵
    static glm::mat4 ComputeWorldTransform(entt::registry& reg, entt::entity entity)
    {
        auto& transform = reg.get<TransformComponent>(entity);
        glm::mat4 localMatrix = transform.GetTransform();

        if (reg.all_of<RelationshipComponent>(entity))
        {
            auto& rel = reg.get<RelationshipComponent>(entity);
            if (static_cast<uint64_t>(rel.ParentID) != 0)
            {
                // 查找父实体
                auto view = reg.view<IDComponent>();
                for (auto e : view)
                {
                    if (view.get<IDComponent>(e).ID == rel.ParentID)
                        return ComputeWorldTransform(reg, e) * localMatrix;
                }
            }
        }

        return localMatrix;
    }

    void MeshRenderSystem::SubmitRenderPackets(
        entt::registry& reg,
        RenderQueue& queue,
        const Ref<Shader>& pbrShader,
        const Ref<Texture2D>& whiteTexture)
    {
        auto meshView = reg.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView)
        {
            auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);

            Mesh* mesh = AssetManager::Get<Mesh>(meshRenderer.MeshAsset);
            if (!mesh)
                continue;

            const auto& subMeshes = mesh->GetSubMeshes();
            auto& cachedMats = meshRenderer.CachedMaterials;

            // 首次或 SubMesh 数量变化时重建缓存
            if (cachedMats.size() != subMeshes.size())
            {
                cachedMats.resize(subMeshes.size());
                for (auto& m : cachedMats)
                    m = CreateRef<Material>(pbrShader);
            }

            // 在 submesh 循环之前检查 VideoPlayerComponent，避免每个 submesh 重复查询
            Ref<Texture2D> videoTex;
            if (reg.any_of<VideoPlayerComponent>(entity))
            {
                auto& vc = reg.get<VideoPlayerComponent>(entity);
                if (vc.RuntimeTexture && vc.IsPlaying)
                    videoTex = vc.RuntimeTexture;
            }

            for (size_t i = 0; i < subMeshes.size(); ++i)
            {
                auto& mat = cachedMats[i];
                const auto& subMesh = subMeshes[i];

                mat->Set("u_Color", meshRenderer.Color);
                mat->Set("u_EntityID", static_cast<int>(entity));
                mat->Set("u_Tiling", meshRenderer.Tiling);
                mat->Set("u_Metallic", meshRenderer.Metallic);
                mat->Set("u_Roughness", meshRenderer.Roughness);

                // Diffuse texture: video > per-submesh > component > white fallback
                Ref<Texture2D> tex = videoTex;

                if (!tex)
                    tex = AssetManager::GetRef<Texture2D>(subMesh.DiffuseTextureAsset);
                if (!tex)
                    tex = AssetManager::GetRef<Texture2D>(meshRenderer.DiffuseTextureAsset);
                if (tex)
                {
                    mat->SetTexture(0, tex);
                    mat->Set("u_HasTexture", 1);
                }
                else
                {
                    mat->SetTexture(0, whiteTexture);
                    mat->Set("u_HasTexture", 0);
                }
                mat->Set("u_DiffuseTexture", 0);

                // Normal map: per-submesh > component > none
                Ref<Texture2D> normalTex = AssetManager::GetRef<Texture2D>(subMesh.NormalTextureAsset);
                if (!normalTex)
                    normalTex = AssetManager::GetRef<Texture2D>(meshRenderer.NormalMapAsset);
                if (normalTex)
                {
                    mat->SetTexture(2, normalTex);
                    mat->Set("u_HasNormalMap", 1);
                }
                else
                {
                    mat->Set("u_HasNormalMap", 0);
                }
                mat->Set("u_NormalMap", 2);

                // Metallic map (unit 3)
                Ref<Texture2D> metallicTex = AssetManager::GetRef<Texture2D>(meshRenderer.MetallicTextureAsset);
                if (metallicTex)
                {
                    mat->SetTexture(3, metallicTex);
                    mat->Set("u_HasMetallicMap", 1);
                }
                else
                {
                    mat->Set("u_HasMetallicMap", 0);
                }
                mat->Set("u_MetallicMap", 3);

                // Roughness map (unit 4)
                Ref<Texture2D> roughnessTex = AssetManager::GetRef<Texture2D>(meshRenderer.RoughnessTextureAsset);
                if (roughnessTex)
                {
                    mat->SetTexture(4, roughnessTex);
                    mat->Set("u_HasRoughnessMap", 1);
                }
                else
                {
                    mat->Set("u_HasRoughnessMap", 0);
                }
                mat->Set("u_RoughnessMap", 4);

                // AO map (unit 5)
                Ref<Texture2D> aoTex = AssetManager::GetRef<Texture2D>(meshRenderer.AOTextureAsset);
                if (aoTex)
                {
                    mat->SetTexture(5, aoTex);
                    mat->Set("u_HasAOMap", 1);
                }
                else
                {
                    mat->Set("u_HasAOMap", 0);
                }
                mat->Set("u_AOMap", 5);

                RenderPacket packet;
                packet.VAO = subMesh.VAO;
                packet.Mat = mat;
                packet.Transform = ComputeWorldTransform(reg, entity);
                packet.EntityID = static_cast<int>(entity);

                queue.Submit(packet);
            }
        }
    }

} // namespace Engine
