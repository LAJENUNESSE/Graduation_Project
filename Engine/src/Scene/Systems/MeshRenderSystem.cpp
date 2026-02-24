#include "engpch.h"
#include "Scene/Systems/MeshRenderSystem.h"
#include "Scene/Components.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"

namespace Engine
{

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

            if (!meshRenderer.MeshData)
                continue;

            for (const auto& subMesh : meshRenderer.MeshData->GetSubMeshes())
            {
                auto mat = CreateRef<Material>(pbrShader);

                mat->Set("u_Color", meshRenderer.Color);
                mat->Set("u_EntityID", static_cast<int>(entity));
                mat->Set("u_Tiling", meshRenderer.Tiling);
                mat->Set("u_Metallic", meshRenderer.Metallic);
                mat->Set("u_Roughness", meshRenderer.Roughness);

                // Diffuse texture: per-submesh > component > white fallback
                Ref<Texture2D> tex = subMesh.DiffuseTexture ? subMesh.DiffuseTexture : meshRenderer.DiffuseTexture;
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
                Ref<Texture2D> normalTex = subMesh.NormalTexture ? subMesh.NormalTexture : meshRenderer.NormalMapTexture;
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
                if (meshRenderer.MetallicTexture)
                {
                    mat->SetTexture(3, meshRenderer.MetallicTexture);
                    mat->Set("u_HasMetallicMap", 1);
                }
                else
                {
                    mat->Set("u_HasMetallicMap", 0);
                }
                mat->Set("u_MetallicMap", 3);

                // Roughness map (unit 4)
                if (meshRenderer.RoughnessTexture)
                {
                    mat->SetTexture(4, meshRenderer.RoughnessTexture);
                    mat->Set("u_HasRoughnessMap", 1);
                }
                else
                {
                    mat->Set("u_HasRoughnessMap", 0);
                }
                mat->Set("u_RoughnessMap", 4);

                // AO map (unit 5)
                if (meshRenderer.AOTexture)
                {
                    mat->SetTexture(5, meshRenderer.AOTexture);
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
                packet.Transform = transform.GetTransform();
                packet.EntityID = static_cast<int>(entity);

                queue.Submit(packet);
            }
        }
    }

} // namespace Engine
