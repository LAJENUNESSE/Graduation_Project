#include "engpch.h"
#include "Scene/Systems/ShadowSystem.h"
#include "Scene/Components.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Mesh.h"
#include "Debug/PerformanceMonitor.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Engine
{

    void ShadowSystem::Init(const ShadowSettings& settings)
    {
        m_Settings = settings;

        m_DepthShader = Shader::Create("assets/shaders/Depth.glsl");

        FramebufferSpecification shadowSpec;
        shadowSpec.Attachments = {FramebufferTextureFormat::DEPTH_COMPONENT};
        shadowSpec.Width = m_Settings.MapResolution;
        shadowSpec.Height = m_Settings.MapResolution;
        m_ShadowMapFBO = Framebuffer::Create(shadowSpec);
    }

    ShadowData ShadowSystem::Execute(entt::registry& reg, const LightEnvironment& lights)
    {
        ShadowData data;

        if (!m_Settings.Enabled)
        {
            PerformanceMonitor::Get().GetShadowPassGPUTimer().Begin();
            PerformanceMonitor::Get().GetShadowPassGPUTimer().End();
            return data;
        }

        PerformanceMonitor::Get().GetShadowPassGPUTimer().Begin();

        // Find the first CastShadows directional light (already sorted by LightSystem)
        glm::vec3 lightDir{0.0f};
        bool found = false;
        for (const auto& dl : lights.DirLights)
        {
            if (dl.CastShadows)
            {
                lightDir = dl.Direction;
                found = true;
                break;
            }
        }

        if (!found)
        {
            PerformanceMonitor::Get().GetShadowPassGPUTimer().End();
            return data;
        }

        data.HasValidShadowCaster = true;

        // Compute light space matrix
        float s = m_Settings.OrthoSize;
        glm::mat4 lightProjection = glm::ortho(-s, s, -s, s, m_Settings.NearPlane, m_Settings.FarPlane);

        glm::vec3 lightPos = -lightDir * m_Settings.OrthoSize;
        glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                            ? glm::vec3(0.0f, 0.0f, 1.0f)
                            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 lightViewMat = glm::lookAt(lightPos, lightPos + lightDir, up);

        data.LightSpaceMatrix = lightProjection * lightViewMat;

        // Render to shadow map
        m_ShadowMapFBO->Bind();
        RenderCommand::Clear();
        RenderCommand::SetCullFaceMode(CullFaceMode::Front);

        m_DepthShader->Bind();
        m_DepthShader->SetMat4("u_LightSpaceMatrix", data.LightSpaceMatrix);

        auto meshView = reg.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView)
        {
            auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);
            if (meshRenderer.MeshData)
            {
                m_DepthShader->SetMat4("u_Transform", transform.GetTransform());
                for (const auto& subMesh : meshRenderer.MeshData->GetSubMeshes())
                {
                    subMesh.VAO->Bind();
                    RenderCommand::DrawIndexed(subMesh.VAO);
                }
            }
        }

        RenderCommand::SetCullFaceMode(CullFaceMode::Back);
        m_ShadowMapFBO->Unbind();

        data.ShadowMapTextureID = m_ShadowMapFBO->GetDepthAttachmentRendererID();

        PerformanceMonitor::Get().GetShadowPassGPUTimer().End();

        return data;
    }

    void ShadowSystem::ResizeShadowMap(int resolution)
    {
        m_Settings.MapResolution = resolution;
        if (m_ShadowMapFBO)
            m_ShadowMapFBO->Resize(resolution, resolution);
    }

} // namespace Engine
