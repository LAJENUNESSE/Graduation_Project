#pragma once

#include "Core/Base.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Shader.h"
#include "Scene/Systems/LightSystem.h"

#include <array>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Engine
{

    class EditorCamera;
    class SceneEntityIndex;
    class WorldTransformCache;

    static constexpr int CSM_MAX_CASCADES = 4;

    struct ShadowSettings
    {
        bool Enabled = true;
        int MapResolution = 1024;
        float Bias = 0.005f;
        float OrthoSize = 20.0f;
        float NearPlane = 0.1f;
        float FarPlane = 50.0f;

        // CSM 参数
        bool CSMEnabled = true;
        int CascadeCount = 3;             // 1~4
        float CascadeSplitLambda = 0.75f; // 对数/均匀分割混合因子
    };

    struct ShadowData
    {
        // 传统单级阴影（CSM 关闭时使用）
        glm::mat4 LightSpaceMatrix{1.0f};
        uint32_t ShadowMapTextureID = 0;
        bool HasValidShadowCaster = false;

        // CSM 数据
        bool CSMActive = false;
        int CascadeCount = 0;
        std::array<glm::mat4, CSM_MAX_CASCADES> CascadeLightSpaceMatrices;
        std::array<float, CSM_MAX_CASCADES> CascadeSplitDepths; // view-space far Z
        uint32_t CascadeShadowMapTexIDs[CSM_MAX_CASCADES] = {};
    };

    class ShadowSystem
    {
    public:
        void Init(const ShadowSettings& settings = {});
        ShadowData Execute(entt::registry& reg, const LightEnvironment& lights, const SceneEntityIndex& index,
                           WorldTransformCache* cache = nullptr);
        ShadowData ExecuteCSM(entt::registry& reg, const LightEnvironment& lights, const EditorCamera& camera,
                              const SceneEntityIndex& index, WorldTransformCache* cache = nullptr);
        void ResizeShadowMap(int resolution);
        ShadowSettings& GetSettings() { return m_Settings; }
        Ref<Shader> GetDepthShader() { return m_DepthShader; }
        Ref<Framebuffer> GetShadowMapFBO() { return m_ShadowMapFBO; }

    private:
        void InitCSMFBOs();
        std::array<float, CSM_MAX_CASCADES + 1> ComputeCascadeSplits(float nearClip, float farClip) const;
        glm::mat4 ComputeCascadeLightSpaceMatrix(const glm::vec3& lightDir, const glm::mat4& invViewProj,
                                                 float nearSplit, float farSplit) const;

        ShadowSettings m_Settings;
        Ref<Shader> m_DepthShader;
        Ref<Framebuffer> m_ShadowMapFBO;

        // CSM FBO（每个级联一个）
        Ref<Framebuffer> m_CascadeFBOs[CSM_MAX_CASCADES];
        bool m_CSMInitialized = false;
    };

} // namespace Engine
