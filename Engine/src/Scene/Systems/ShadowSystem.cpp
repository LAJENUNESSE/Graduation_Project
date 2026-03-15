#include "engpch.h"
#include "Scene/Systems/ShadowSystem.h"
#include "Asset/AssetManager.h"
#include "Debug/PerformanceMonitor.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/Mesh.h"
#include "Renderer/RenderCommand.h"
#include "Scene/Components.h"
#include "Scene/WorldTransformService.h"
#include "Scene/SceneEntityIndex.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Engine
{

    // 渲染所有网格到当前绑定的深度 FBO
    static void RenderMeshesToDepth(entt::registry& reg, const Ref<Shader>& depthShader, const glm::mat4& lightSpaceMat,
                                    const SceneEntityIndex& index, WorldTransformCache* cache)
    {
        depthShader->Bind();
        depthShader->SetMat4("u_LightSpaceMatrix", lightSpaceMat);

        auto meshView = reg.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView)
        {
            auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);
            Mesh* mesh = AssetManager::Get<Mesh>(meshRenderer.MeshAsset);
            if (mesh)
            {
                depthShader->SetMat4("u_Transform", WorldTransformService::ComputeWorldTransform(reg, entity, index, cache));
                for (const auto& subMesh : mesh->GetSubMeshes())
                {
                    subMesh.VAO->Bind();
                    RenderCommand::DrawIndexed(subMesh.VAO);
                }
            }
        }
    }

    void ShadowSystem::Init(const ShadowSettings& settings)
    {
        m_Settings = settings;

        m_DepthShader = Shader::Create("assets/shaders/Depth.glsl");

        FramebufferSpecification shadowSpec;
        shadowSpec.Attachments = {FramebufferTextureFormat::DEPTH_COMPONENT};
        shadowSpec.Width = m_Settings.MapResolution;
        shadowSpec.Height = m_Settings.MapResolution;
        m_ShadowMapFBO = Framebuffer::Create(shadowSpec);

        InitCSMFBOs();
    }

    void ShadowSystem::InitCSMFBOs()
    {
        for (int i = 0; i < CSM_MAX_CASCADES; ++i)
        {
            FramebufferSpecification spec;
            spec.Attachments = {FramebufferTextureFormat::DEPTH_COMPONENT};
            spec.Width = m_Settings.MapResolution;
            spec.Height = m_Settings.MapResolution;
            m_CascadeFBOs[i] = Framebuffer::Create(spec);
        }
        m_CSMInitialized = true;
    }

    std::array<float, CSM_MAX_CASCADES + 1> ShadowSystem::ComputeCascadeSplits(float nearClip, float farClip) const
    {
        std::array<float, CSM_MAX_CASCADES + 1> splits;
        int count = std::min(m_Settings.CascadeCount, CSM_MAX_CASCADES);
        float lambda = m_Settings.CascadeSplitLambda;

        splits[0] = nearClip;
        for (int i = 1; i <= count; ++i)
        {
            float p = static_cast<float>(i) / static_cast<float>(count);
            // 对数分割
            float logSplit = nearClip * std::pow(farClip / nearClip, p);
            // 均匀分割
            float uniformSplit = nearClip + (farClip - nearClip) * p;
            // 混合
            splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
        }

        return splits;
    }

    glm::mat4 ShadowSystem::ComputeCascadeLightSpaceMatrix(const glm::vec3& lightDir, const glm::mat4& invViewProj,
                                                           float nearSplit, float farSplit) const
    {
        // 计算此分割的子视锥角点（NDC -> world）
        // 将 nearSplit/farSplit 映射到 NDC Z
        // 使用 invViewProj 将 NDC 角点变换到世界空间

        // 8 个角点
        glm::vec4 frustumCorners[8] = {
            // 近平面 4 个角
            {-1.0f, -1.0f, -1.0f, 1.0f},
            {1.0f, -1.0f, -1.0f, 1.0f},
            {1.0f, 1.0f, -1.0f, 1.0f},
            {-1.0f, 1.0f, -1.0f, 1.0f},
            // 远平面 4 个角
            {-1.0f, -1.0f, 1.0f, 1.0f},
            {1.0f, -1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f, 1.0f},
            {-1.0f, 1.0f, 1.0f, 1.0f},
        };

        // NDC -> world
        glm::vec3 worldCorners[8];
        for (int i = 0; i < 8; i++)
        {
            glm::vec4 pt = invViewProj * frustumCorners[i];
            worldCorners[i] = glm::vec3(pt) / pt.w;
        }

        // 使用 nearSplit 和 farSplit 对近远面插值
        // 原始近/远面是完整视锥的，需要按分割比例插值
        for (int i = 0; i < 4; i++)
        {
            glm::vec3 ray = worldCorners[i + 4] - worldCorners[i];
            // 重新定位近面角点
            glm::vec3 nearCorner = worldCorners[i]; // 暂存
            worldCorners[i + 4] = nearCorner + ray * farSplit;
            worldCorners[i] = nearCorner + ray * nearSplit;
        }

        // 计算级联视锥的中心
        glm::vec3 center(0.0f);
        for (int i = 0; i < 8; i++)
            center += worldCorners[i];
        center /= 8.0f;

        // 光源视图矩阵
        glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                           ? glm::vec3(0.0f, 0.0f, 1.0f)
                           : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 lightView = glm::lookAt(center - lightDir, center, up);

        // 计算在光源空间中的包围盒
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();

        for (int i = 0; i < 8; i++)
        {
            glm::vec4 lsCorner = lightView * glm::vec4(worldCorners[i], 1.0f);
            minX = std::min(minX, lsCorner.x);
            maxX = std::max(maxX, lsCorner.x);
            minY = std::min(minY, lsCorner.y);
            maxY = std::max(maxY, lsCorner.y);
            minZ = std::min(minZ, lsCorner.z);
            maxZ = std::max(maxZ, lsCorner.z);
        }

        // 适当扩展 Z 范围以包含在视锥外但能投射阴影的物体
        float zExtend = (maxZ - minZ) * 1.5f;
        minZ -= zExtend;

        glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
        return lightProj * lightView;
    }

    ShadowData ShadowSystem::Execute(entt::registry& reg, const LightEnvironment& lights, const SceneEntityIndex& index,
                                      WorldTransformCache* cache)
    {
        ShadowData data;

        if (!m_Settings.Enabled)
        {
            PerformanceMonitor::Get().GetShadowPassGPUTimer().Begin();
            PerformanceMonitor::Get().GetShadowPassGPUTimer().End();
            return data;
        }

        PerformanceMonitor::Get().GetShadowPassGPUTimer().Begin();

        // 查找第一个 CastShadows 方向光
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

        // 计算 light space 矩阵（传统单级阴影，CSM 关闭时使用）
        float s = m_Settings.OrthoSize;
        glm::mat4 lightProjection = glm::ortho(-s, s, -s, s, m_Settings.NearPlane, m_Settings.FarPlane);

        glm::vec3 lightPos = -lightDir * m_Settings.OrthoSize;
        glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                           ? glm::vec3(0.0f, 0.0f, 1.0f)
                           : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 lightViewMat = glm::lookAt(lightPos, lightPos + lightDir, up);

        data.LightSpaceMatrix = lightProjection * lightViewMat;

        // 渲染到 shadow map
        m_ShadowMapFBO->Bind();
        RenderCommand::Clear();
        RenderCommand::SetCullFaceMode(CullFaceMode::Front);

        RenderMeshesToDepth(reg, m_DepthShader, data.LightSpaceMatrix, index, cache);

        RenderCommand::SetCullFaceMode(CullFaceMode::Back);
        m_ShadowMapFBO->Unbind();

        data.ShadowMapTextureID = m_ShadowMapFBO->GetDepthAttachmentRendererID();

        PerformanceMonitor::Get().GetShadowPassGPUTimer().End();

        return data;
    }

    ShadowData ShadowSystem::ExecuteCSM(entt::registry& reg, const LightEnvironment& lights, const EditorCamera& camera,
                                         const SceneEntityIndex& index, WorldTransformCache* cache)
    {
        ShadowData data;

        if (!m_Settings.Enabled)
        {
            PerformanceMonitor::Get().GetShadowPassGPUTimer().Begin();
            PerformanceMonitor::Get().GetShadowPassGPUTimer().End();
            return data;
        }

        if (!m_CSMInitialized)
            InitCSMFBOs();

        PerformanceMonitor::Get().GetShadowPassGPUTimer().Begin();

        // 查找第一个 CastShadows 方向光
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

        if (!m_Settings.CSMEnabled)
        {
            // 退回到传统单级阴影
            float s = m_Settings.OrthoSize;
            glm::mat4 lightProjection = glm::ortho(-s, s, -s, s, m_Settings.NearPlane, m_Settings.FarPlane);
            glm::vec3 lightPos = -lightDir * m_Settings.OrthoSize;
            glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                               ? glm::vec3(0.0f, 0.0f, 1.0f)
                               : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::mat4 lightViewMat = glm::lookAt(lightPos, lightPos + lightDir, up);
            data.LightSpaceMatrix = lightProjection * lightViewMat;

            m_ShadowMapFBO->Bind();
            RenderCommand::Clear();
            RenderCommand::SetCullFaceMode(CullFaceMode::Front);
            RenderMeshesToDepth(reg, m_DepthShader, data.LightSpaceMatrix, index, cache);
            RenderCommand::SetCullFaceMode(CullFaceMode::Back);
            m_ShadowMapFBO->Unbind();

            data.ShadowMapTextureID = m_ShadowMapFBO->GetDepthAttachmentRendererID();
            data.CSMActive = false;

            PerformanceMonitor::Get().GetShadowPassGPUTimer().End();
            return data;
        }

        // CSM: 级联阴影贴图
        data.CSMActive = true;
        int cascadeCount = std::min(m_Settings.CascadeCount, CSM_MAX_CASCADES);
        data.CascadeCount = cascadeCount;

        // 计算级联分割
        float nearClip = camera.GetNearClip();
        float farClip = std::min(camera.GetFarClip(), 200.0f); // 限制阴影最远距离
        auto splits = ComputeCascadeSplits(nearClip, farClip);

        // 使用限制后的 near/far 构建专用投影矩阵，确保 NDC 角点对应正确的视锥范围
        glm::mat4 shadowProjection =
            glm::perspective(glm::radians(camera.GetFOV()), camera.GetAspectRatio(), nearClip, farClip);
        glm::mat4 invViewProj = glm::inverse(shadowProjection * camera.GetViewMatrix());

        // 为每个级联计算 lightSpaceMatrix 并渲染深度
        for (int i = 0; i < cascadeCount; i++)
        {
            // 将分割点归一化到 [0, 1] 范围（相对于完整视锥）
            float cascadeNear = (splits[i] - nearClip) / (farClip - nearClip);
            float cascadeFar = (splits[i + 1] - nearClip) / (farClip - nearClip);

            data.CascadeLightSpaceMatrices[i] =
                ComputeCascadeLightSpaceMatrix(lightDir, invViewProj, cascadeNear, cascadeFar);
            data.CascadeSplitDepths[i] = splits[i + 1]; // view-space 远裁切

            // 检查 FBO 分辨率
            auto& fboSpec = m_CascadeFBOs[i]->GetSpecification();
            if (fboSpec.Width != static_cast<uint32_t>(m_Settings.MapResolution))
                m_CascadeFBOs[i]->Resize(m_Settings.MapResolution, m_Settings.MapResolution);

            m_CascadeFBOs[i]->Bind();
            RenderCommand::Clear();
            RenderCommand::SetCullFaceMode(CullFaceMode::Front);

            RenderMeshesToDepth(reg, m_DepthShader, data.CascadeLightSpaceMatrices[i], index, cache);

            RenderCommand::SetCullFaceMode(CullFaceMode::Back);
            m_CascadeFBOs[i]->Unbind();

            data.CascadeShadowMapTexIDs[i] = m_CascadeFBOs[i]->GetDepthAttachmentRendererID();
        }

        // 同时设置传统数据（兼容性 fallback：使用第一个级联）
        data.LightSpaceMatrix = data.CascadeLightSpaceMatrices[0];
        data.ShadowMapTextureID = data.CascadeShadowMapTexIDs[0];

        PerformanceMonitor::Get().GetShadowPassGPUTimer().End();

        return data;
    }

    void ShadowSystem::ResizeShadowMap(int resolution)
    {
        m_Settings.MapResolution = resolution;
        if (m_ShadowMapFBO)
            m_ShadowMapFBO->Resize(resolution, resolution);

        for (int i = 0; i < CSM_MAX_CASCADES; ++i)
        {
            if (m_CascadeFBOs[i])
                m_CascadeFBOs[i]->Resize(resolution, resolution);
        }
    }

} // namespace Engine
