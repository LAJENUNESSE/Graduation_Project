#include "engpch.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shader.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Renderer/EditorCamera.h"

namespace Engine
{

    Scene::Scene()
    {
        // Create default mesh shader (Phong lighting with multi-light support)
        std::string vertexSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec3 a_Normal;
            layout(location = 2) in vec2 a_TexCoords;

            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;

            out vec3 v_Normal;
            out vec3 v_FragPos;
            out vec2 v_TexCoord;

            void main() {
                v_Normal = mat3(transpose(inverse(u_Transform))) * a_Normal;
                v_FragPos = vec3(u_Transform * vec4(a_Position, 1.0));
                v_TexCoord = a_TexCoords;
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
            }
        )";

        std::string fragmentSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 o_Color;
            layout(location = 1) out int o_EntityID;

            #define MAX_DIR_LIGHTS   2
            #define MAX_POINT_LIGHTS 8
            #define MAX_SPOT_LIGHTS  4

            struct DirLight {
                vec3 direction;
                vec3 color;
                float intensity;
            };

            struct PointLight {
                vec3 position;
                vec3 color;
                float intensity;
                float constant;
                float linear;
                float quadratic;
            };

            struct SpotLight {
                vec3 position;
                vec3 direction;
                vec3 color;
                float intensity;
                float constant;
                float linear;
                float quadratic;
                float innerCutoff;
                float outerCutoff;
            };

            uniform vec4 u_Color;
            uniform int u_EntityID;
            uniform vec3 u_ViewPos;
            uniform float u_AmbientStrength;

            uniform sampler2D u_DiffuseTexture;
            uniform int u_HasTexture;
            uniform vec2 u_Tiling;
            uniform float u_Shininess;

            uniform int u_NumDirLights;
            uniform int u_NumPointLights;
            uniform int u_NumSpotLights;

            uniform DirLight   u_DirLights[MAX_DIR_LIGHTS];
            uniform PointLight u_PointLights[MAX_POINT_LIGHTS];
            uniform SpotLight  u_SpotLights[MAX_SPOT_LIGHTS];

            in vec3 v_Normal;
            in vec3 v_FragPos;
            in vec2 v_TexCoord;

            vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, float shininess)
            {
                vec3 lightDir = normalize(-light.direction);
                // Diffuse
                float diff = max(dot(normal, lightDir), 0.0);
                // Specular (Blinn-Phong)
                vec3 halfwayDir = normalize(lightDir + viewDir);
                float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);

                vec3 diffuse  = light.color * light.intensity * diff;
                vec3 specular = light.color * light.intensity * spec;
                return diffuse + specular;
            }

            vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, float shininess)
            {
                vec3 lightDir = normalize(light.position - fragPos);
                // Diffuse
                float diff = max(dot(normal, lightDir), 0.0);
                // Specular (Blinn-Phong)
                vec3 halfwayDir = normalize(lightDir + viewDir);
                float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
                // Attenuation
                float distance = length(light.position - fragPos);
                float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * distance * distance);

                vec3 diffuse  = light.color * light.intensity * diff * attenuation;
                vec3 specular = light.color * light.intensity * spec * attenuation;
                return diffuse + specular;
            }

            vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir, float shininess)
            {
                vec3 lightDir = normalize(light.position - fragPos);
                // Diffuse
                float diff = max(dot(normal, lightDir), 0.0);
                // Specular (Blinn-Phong)
                vec3 halfwayDir = normalize(lightDir + viewDir);
                float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
                // Attenuation
                float distance = length(light.position - fragPos);
                float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * distance * distance);
                // Spotlight cone (innerCutoff/outerCutoff are already cos values)
                float theta = dot(lightDir, normalize(-light.direction));
                float epsilon = light.innerCutoff - light.outerCutoff;
                float spotIntensity = (abs(epsilon) < 0.0001) ? step(light.innerCutoff, theta) : clamp((theta - light.outerCutoff) / epsilon, 0.0, 1.0);

                vec3 diffuse  = light.color * light.intensity * diff * attenuation * spotIntensity;
                vec3 specular = light.color * light.intensity * spec * attenuation * spotIntensity;
                return diffuse + specular;
            }

            void main() {
                vec3 norm = normalize(v_Normal);
                vec3 viewDir = normalize(u_ViewPos - v_FragPos);

                // Texture sampling
                vec4 texColor = (u_HasTexture != 0) ? texture(u_DiffuseTexture, v_TexCoord * u_Tiling) : vec4(1.0);
                vec4 baseColor = texColor * u_Color;

                // Ambient (not pre-multiplied by color, applied once at the end)
                vec3 ambient = vec3(u_AmbientStrength);

                // Accumulate lighting from all sources
                vec3 result = vec3(0.0);
                for (int i = 0; i < u_NumDirLights; i++)
                    result += CalcDirLight(u_DirLights[i], norm, viewDir, u_Shininess);
                for (int i = 0; i < u_NumPointLights; i++)
                    result += CalcPointLight(u_PointLights[i], norm, v_FragPos, viewDir, u_Shininess);
                for (int i = 0; i < u_NumSpotLights; i++)
                    result += CalcSpotLight(u_SpotLights[i], norm, v_FragPos, viewDir, u_Shininess);

                o_Color = vec4((ambient + result) * baseColor.rgb, baseColor.a);
                o_EntityID = u_EntityID;
            }
        )";

        m_MeshShader = Shader::Create("MeshShader", vertexSrc, fragmentSrc);

        // Create 1x1 white default texture
        m_WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteData = 0xFFFFFFFF;
        m_WhiteTexture->SetData(&whiteData, sizeof(uint32_t));
    }

    Scene::~Scene()
    {
    }

    Entity Scene::CreateEntity(const std::string& name)
    {
        return CreateEntityWithUUID(UUID(), name);
    }

    Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
    {
        Entity entity = {m_Registry.create(), this};
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<TagComponent>(name);
        entity.AddComponent<TransformComponent>();
        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        m_Registry.destroy(entity);
    }

    void Scene::OnUpdateEditor(Timestep ts, EditorCamera& camera)
    {
        Renderer::BeginScene(camera.GetViewProjection());

        // Collect and upload lights
        m_MeshShader->Bind();
        m_MeshShader->SetFloat3("u_ViewPos", camera.GetPosition());
        m_MeshShader->SetFloat("u_AmbientStrength", 0.15f);

        int numDirLights = 0;
        int numPointLights = 0;
        int numSpotLights = 0;

        auto lightView = m_Registry.view<TransformComponent, LightComponent>();
        for (auto entity : lightView)
        {
            auto [transform, light] = lightView.get<TransformComponent, LightComponent>(entity);
            glm::vec3 forward = glm::normalize(glm::quat(transform.Rotation) * glm::vec3(0.0f, 0.0f, -1.0f));

            switch (light.Type)
            {
            case LightComponent::LightType::Directional:
            {
                if (numDirLights >= 2)
                    break;
                std::string prefix = "u_DirLights[" + std::to_string(numDirLights) + "]";
                m_MeshShader->SetFloat3(prefix + ".direction", forward);
                m_MeshShader->SetFloat3(prefix + ".color", light.Color);
                m_MeshShader->SetFloat(prefix + ".intensity", light.Intensity);
                numDirLights++;
                break;
            }
            case LightComponent::LightType::Point:
            {
                if (numPointLights >= 8)
                    break;
                std::string prefix = "u_PointLights[" + std::to_string(numPointLights) + "]";
                m_MeshShader->SetFloat3(prefix + ".position", transform.Translation);
                m_MeshShader->SetFloat3(prefix + ".color", light.Color);
                m_MeshShader->SetFloat(prefix + ".intensity", light.Intensity);
                m_MeshShader->SetFloat(prefix + ".constant", light.Constant);
                m_MeshShader->SetFloat(prefix + ".linear", light.Linear);
                m_MeshShader->SetFloat(prefix + ".quadratic", light.Quadratic);
                numPointLights++;
                break;
            }
            case LightComponent::LightType::Spot:
            {
                if (numSpotLights >= 4)
                    break;
                std::string prefix = "u_SpotLights[" + std::to_string(numSpotLights) + "]";
                m_MeshShader->SetFloat3(prefix + ".position", transform.Translation);
                m_MeshShader->SetFloat3(prefix + ".direction", forward);
                m_MeshShader->SetFloat3(prefix + ".color", light.Color);
                m_MeshShader->SetFloat(prefix + ".intensity", light.Intensity);
                m_MeshShader->SetFloat(prefix + ".constant", light.Constant);
                m_MeshShader->SetFloat(prefix + ".linear", light.Linear);
                m_MeshShader->SetFloat(prefix + ".quadratic", light.Quadratic);
                m_MeshShader->SetFloat(prefix + ".innerCutoff", std::cos(light.InnerCutoff));
                m_MeshShader->SetFloat(prefix + ".outerCutoff", std::cos(light.OuterCutoff));
                numSpotLights++;
                break;
            }
            }
        }

        m_MeshShader->SetInt("u_NumDirLights", numDirLights);
        m_MeshShader->SetInt("u_NumPointLights", numPointLights);
        m_MeshShader->SetInt("u_NumSpotLights", numSpotLights);

        // Render meshes
        auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView)
        {
            auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);

            if (meshRenderer.MeshData)
            {
                // Bind texture
                if (meshRenderer.DiffuseTexture)
                {
                    meshRenderer.DiffuseTexture->Bind(0);
                    m_MeshShader->SetInt("u_HasTexture", 1);
                }
                else
                {
                    m_WhiteTexture->Bind(0);
                    m_MeshShader->SetInt("u_HasTexture", 0);
                }
                m_MeshShader->SetInt("u_DiffuseTexture", 0);
                m_MeshShader->SetFloat2("u_Tiling", meshRenderer.Tiling);
                m_MeshShader->SetFloat("u_Shininess", meshRenderer.Shininess);

                m_MeshShader->SetFloat4("u_Color", meshRenderer.Color);
                m_MeshShader->SetInt("u_EntityID", static_cast<int>(entity));
                Renderer::Submit(m_MeshShader, meshRenderer.MeshData->GetVertexArray(),
                                 transform.GetTransform());
            }
        }

        Renderer::EndScene();
    }

    void Scene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;

        // Resize non-fixed-aspect-ratio cameras
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraComponent = view.get<CameraComponent>(entity);
            if (!cameraComponent.FixedAspectRatio)
            {
                cameraComponent.Camera.SetViewportSize(width, height);
            }
        }
    }

} // namespace Engine
