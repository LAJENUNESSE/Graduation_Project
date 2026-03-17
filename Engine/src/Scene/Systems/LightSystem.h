#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace Engine
{

    struct LightEnvironment
    {
        struct DirLight
        {
            glm::vec3 Direction;
            glm::vec3 Color;
            float     Intensity;
            bool      CastShadows;
        };

        struct PointLight
        {
            glm::vec3 Position;
            glm::vec3 Color;
            float     Intensity;
            float     Constant;
            float     Linear;
            float     Quadratic;
        };

        struct SpotLight
        {
            glm::vec3 Position;
            glm::vec3 Direction;
            glm::vec3 Color;
            float     Intensity;
            float     Constant;
            float     Linear;
            float     Quadratic;
            float     InnerCutoff;
            float     OuterCutoff;
        };

        std::vector<DirLight>   DirLights;   // max 2
        std::vector<PointLight> PointLights; // max 8
        std::vector<SpotLight>  SpotLights;  // max 4
        float                   AmbientStrength = 0.3f;
    };

    class SceneEntityIndex;
    class WorldTransformCache;

    class LightSystem
    {
    public:
        static LightEnvironment
        CollectLights(entt::registry& reg, const SceneEntityIndex& index, WorldTransformCache* cache = nullptr);
        static void UploadToShader(const Ref<Shader>& shader, const LightEnvironment& env);
    };

} // namespace Engine
