#include "Scene/Systems/LightSystem.h"
#include "Scene/Components.h"
#include "engpch.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

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

    LightEnvironment LightSystem::CollectLights(entt::registry& reg)
    {
        LightEnvironment env;

        auto lightView = reg.view<TransformComponent, LightComponent>();
        for (auto entity : lightView)
        {
            auto& light = lightView.get<LightComponent>(entity);

            // 使用世界变换计算光源位置和方向
            glm::mat4 worldMat = ComputeWorldTransform(reg, entity);
            glm::vec3 worldPos = glm::vec3(worldMat[3]);
            glm::vec3 forward = glm::normalize(glm::mat3(worldMat) * glm::vec3(0.0f, 0.0f, -1.0f));

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
                    env.PointLights.push_back(
                        {worldPos, light.Color, light.Intensity, light.Constant, light.Linear, light.Quadratic});
                break;
            }
            case LightComponent::LightType::Spot:
            {
                if (env.SpotLights.size() < 4)
                    env.SpotLights.push_back({worldPos, forward, light.Color, light.Intensity, light.Constant,
                                              light.Linear, light.Quadratic, std::cos(light.InnerCutoff),
                                              std::cos(light.OuterCutoff)});
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
        // 预生成的 uniform 名称数组，避免每帧循环内的字符串拼接和临时分配

        // DirLights (max 2)
        static const char* s_DirLightDirection[] = {"u_DirLights[0].direction", "u_DirLights[1].direction"};
        static const char* s_DirLightColor[] = {"u_DirLights[0].color", "u_DirLights[1].color"};
        static const char* s_DirLightIntensity[] = {"u_DirLights[0].intensity", "u_DirLights[1].intensity"};

        // PointLights (max 8)
        static const char* s_PointLightPosition[] = {"u_PointLights[0].position", "u_PointLights[1].position",
                                                     "u_PointLights[2].position", "u_PointLights[3].position",
                                                     "u_PointLights[4].position", "u_PointLights[5].position",
                                                     "u_PointLights[6].position", "u_PointLights[7].position"};
        static const char* s_PointLightColor[] = {
            "u_PointLights[0].color", "u_PointLights[1].color", "u_PointLights[2].color", "u_PointLights[3].color",
            "u_PointLights[4].color", "u_PointLights[5].color", "u_PointLights[6].color", "u_PointLights[7].color"};
        static const char* s_PointLightIntensity[] = {"u_PointLights[0].intensity", "u_PointLights[1].intensity",
                                                      "u_PointLights[2].intensity", "u_PointLights[3].intensity",
                                                      "u_PointLights[4].intensity", "u_PointLights[5].intensity",
                                                      "u_PointLights[6].intensity", "u_PointLights[7].intensity"};
        static const char* s_PointLightConstant[] = {"u_PointLights[0].constant", "u_PointLights[1].constant",
                                                     "u_PointLights[2].constant", "u_PointLights[3].constant",
                                                     "u_PointLights[4].constant", "u_PointLights[5].constant",
                                                     "u_PointLights[6].constant", "u_PointLights[7].constant"};
        static const char* s_PointLightLinear[] = {
            "u_PointLights[0].linear", "u_PointLights[1].linear", "u_PointLights[2].linear", "u_PointLights[3].linear",
            "u_PointLights[4].linear", "u_PointLights[5].linear", "u_PointLights[6].linear", "u_PointLights[7].linear"};
        static const char* s_PointLightQuadratic[] = {"u_PointLights[0].quadratic", "u_PointLights[1].quadratic",
                                                      "u_PointLights[2].quadratic", "u_PointLights[3].quadratic",
                                                      "u_PointLights[4].quadratic", "u_PointLights[5].quadratic",
                                                      "u_PointLights[6].quadratic", "u_PointLights[7].quadratic"};

        // SpotLights (max 4)
        static const char* s_SpotLightPosition[] = {"u_SpotLights[0].position", "u_SpotLights[1].position",
                                                    "u_SpotLights[2].position", "u_SpotLights[3].position"};
        static const char* s_SpotLightDirection[] = {"u_SpotLights[0].direction", "u_SpotLights[1].direction",
                                                     "u_SpotLights[2].direction", "u_SpotLights[3].direction"};
        static const char* s_SpotLightColor[] = {"u_SpotLights[0].color", "u_SpotLights[1].color",
                                                 "u_SpotLights[2].color", "u_SpotLights[3].color"};
        static const char* s_SpotLightIntensity[] = {"u_SpotLights[0].intensity", "u_SpotLights[1].intensity",
                                                     "u_SpotLights[2].intensity", "u_SpotLights[3].intensity"};
        static const char* s_SpotLightConstant[] = {"u_SpotLights[0].constant", "u_SpotLights[1].constant",
                                                    "u_SpotLights[2].constant", "u_SpotLights[3].constant"};
        static const char* s_SpotLightLinear[] = {"u_SpotLights[0].linear", "u_SpotLights[1].linear",
                                                  "u_SpotLights[2].linear", "u_SpotLights[3].linear"};
        static const char* s_SpotLightQuadratic[] = {"u_SpotLights[0].quadratic", "u_SpotLights[1].quadratic",
                                                     "u_SpotLights[2].quadratic", "u_SpotLights[3].quadratic"};
        static const char* s_SpotLightInnerCutoff[] = {"u_SpotLights[0].innerCutoff", "u_SpotLights[1].innerCutoff",
                                                       "u_SpotLights[2].innerCutoff", "u_SpotLights[3].innerCutoff"};
        static const char* s_SpotLightOuterCutoff[] = {"u_SpotLights[0].outerCutoff", "u_SpotLights[1].outerCutoff",
                                                       "u_SpotLights[2].outerCutoff", "u_SpotLights[3].outerCutoff"};

        int numDirLights = static_cast<int>(env.DirLights.size());
        for (int i = 0; i < numDirLights; i++)
        {
            shader->SetFloat3(s_DirLightDirection[i], env.DirLights[i].Direction);
            shader->SetFloat3(s_DirLightColor[i], env.DirLights[i].Color);
            shader->SetFloat(s_DirLightIntensity[i], env.DirLights[i].Intensity);
        }

        int numPointLights = static_cast<int>(env.PointLights.size());
        for (int i = 0; i < numPointLights; i++)
        {
            shader->SetFloat3(s_PointLightPosition[i], env.PointLights[i].Position);
            shader->SetFloat3(s_PointLightColor[i], env.PointLights[i].Color);
            shader->SetFloat(s_PointLightIntensity[i], env.PointLights[i].Intensity);
            shader->SetFloat(s_PointLightConstant[i], env.PointLights[i].Constant);
            shader->SetFloat(s_PointLightLinear[i], env.PointLights[i].Linear);
            shader->SetFloat(s_PointLightQuadratic[i], env.PointLights[i].Quadratic);
        }

        int numSpotLights = static_cast<int>(env.SpotLights.size());
        for (int i = 0; i < numSpotLights; i++)
        {
            shader->SetFloat3(s_SpotLightPosition[i], env.SpotLights[i].Position);
            shader->SetFloat3(s_SpotLightDirection[i], env.SpotLights[i].Direction);
            shader->SetFloat3(s_SpotLightColor[i], env.SpotLights[i].Color);
            shader->SetFloat(s_SpotLightIntensity[i], env.SpotLights[i].Intensity);
            shader->SetFloat(s_SpotLightConstant[i], env.SpotLights[i].Constant);
            shader->SetFloat(s_SpotLightLinear[i], env.SpotLights[i].Linear);
            shader->SetFloat(s_SpotLightQuadratic[i], env.SpotLights[i].Quadratic);
            shader->SetFloat(s_SpotLightInnerCutoff[i], env.SpotLights[i].InnerCutoff);
            shader->SetFloat(s_SpotLightOuterCutoff[i], env.SpotLights[i].OuterCutoff);
        }

        shader->SetInt("u_NumDirLights", numDirLights);
        shader->SetInt("u_NumPointLights", numPointLights);
        shader->SetInt("u_NumSpotLights", numSpotLights);
        shader->SetFloat("u_AmbientStrength", env.AmbientStrength);
    }

} // namespace Engine
