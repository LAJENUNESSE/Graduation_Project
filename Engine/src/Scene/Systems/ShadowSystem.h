#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/Framebuffer.h"
#include "Scene/Systems/LightSystem.h"

#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace Engine
{

    struct ShadowSettings
    {
        bool Enabled = true;
        int MapResolution = 1024;
        float Bias = 0.005f;
        float OrthoSize = 20.0f;
        float NearPlane = 0.1f;
        float FarPlane = 50.0f;
    };

    struct ShadowData
    {
        glm::mat4 LightSpaceMatrix{1.0f};
        uint32_t ShadowMapTextureID = 0;
        bool HasValidShadowCaster = false;
    };

    class ShadowSystem
    {
    public:
        void Init(const ShadowSettings& settings = {});
        ShadowData Execute(entt::registry& reg, const LightEnvironment& lights);
        void ResizeShadowMap(int resolution);
        ShadowSettings& GetSettings() { return m_Settings; }

    private:
        ShadowSettings m_Settings;
        Ref<Shader> m_DepthShader;
        Ref<Framebuffer> m_ShadowMapFBO;
    };

} // namespace Engine
