#include "engpch.h"
#include "Scene/Systems/LightSystem.h"
#include "Scene/Components.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

namespace Engine
{

    LightEnvironment LightSystem::CollectLights(entt::registry& reg)
    {
        LightEnvironment env;

        auto lightView = reg.view<TransformComponent, LightComponent>();
        for (auto entity : lightView)
        {
            auto [transform, light] = lightView.get<TransformComponent, LightComponent>(entity);
            glm::vec3 forward = glm::normalize(glm::quat(transform.Rotation) * glm::vec3(0.0f, 0.0f, -1.0f));

            switch (light.Type)
            {
            case LightComponent::LightType::Directional:
            {
                if (env.DirLights.size() < 2)
                    env.DirLights.push_back({forward, light.Color, light.Intensity, light.CastShadows});
                break;
            }
            case LightComponent::LightType::Point:
            {
                if (env.PointLights.size() < 8)
                    env.PointLights.push_back({transform.Translation, light.Color, light.Intensity,
                                                light.Constant, light.Linear, light.Quadratic});
                break;
            }
            case LightComponent::LightType::Spot:
            {
                if (env.SpotLights.size() < 4)
                    env.SpotLights.push_back({transform.Translation, forward, light.Color, light.Intensity,
                                               light.Constant, light.Linear, light.Quadratic,
                                               std::cos(light.InnerCutoff), std::cos(light.OuterCutoff)});
                break;
            }
            }
        }

        // Sort directional lights: CastShadows=true first
        std::stable_sort(env.DirLights.begin(), env.DirLights.end(),
                         [](const LightEnvironment::DirLight& a, const LightEnvironment::DirLight& b)
                         { return a.CastShadows > b.CastShadows; });

        return env;
    }

    void LightSystem::UploadToShader(const Ref<Shader>& shader, const LightEnvironment& env)
    {
        int numDirLights = static_cast<int>(env.DirLights.size());
        for (int i = 0; i < numDirLights; i++)
        {
            std::string prefix = "u_DirLights[" + std::to_string(i) + "]";
            shader->SetFloat3(prefix + ".direction", env.DirLights[i].Direction);
            shader->SetFloat3(prefix + ".color", env.DirLights[i].Color);
            shader->SetFloat(prefix + ".intensity", env.DirLights[i].Intensity);
        }

        int numPointLights = static_cast<int>(env.PointLights.size());
        for (int i = 0; i < numPointLights; i++)
        {
            std::string prefix = "u_PointLights[" + std::to_string(i) + "]";
            shader->SetFloat3(prefix + ".position", env.PointLights[i].Position);
            shader->SetFloat3(prefix + ".color", env.PointLights[i].Color);
            shader->SetFloat(prefix + ".intensity", env.PointLights[i].Intensity);
            shader->SetFloat(prefix + ".constant", env.PointLights[i].Constant);
            shader->SetFloat(prefix + ".linear", env.PointLights[i].Linear);
            shader->SetFloat(prefix + ".quadratic", env.PointLights[i].Quadratic);
        }

        int numSpotLights = static_cast<int>(env.SpotLights.size());
        for (int i = 0; i < numSpotLights; i++)
        {
            std::string prefix = "u_SpotLights[" + std::to_string(i) + "]";
            shader->SetFloat3(prefix + ".position", env.SpotLights[i].Position);
            shader->SetFloat3(prefix + ".direction", env.SpotLights[i].Direction);
            shader->SetFloat3(prefix + ".color", env.SpotLights[i].Color);
            shader->SetFloat(prefix + ".intensity", env.SpotLights[i].Intensity);
            shader->SetFloat(prefix + ".constant", env.SpotLights[i].Constant);
            shader->SetFloat(prefix + ".linear", env.SpotLights[i].Linear);
            shader->SetFloat(prefix + ".quadratic", env.SpotLights[i].Quadratic);
            shader->SetFloat(prefix + ".innerCutoff", env.SpotLights[i].InnerCutoff);
            shader->SetFloat(prefix + ".outerCutoff", env.SpotLights[i].OuterCutoff);
        }

        shader->SetInt("u_NumDirLights", numDirLights);
        shader->SetInt("u_NumPointLights", numPointLights);
        shader->SetInt("u_NumSpotLights", numSpotLights);
        shader->SetFloat("u_AmbientStrength", env.AmbientStrength);
    }

} // namespace Engine
