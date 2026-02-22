#include "engpch.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Shader.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/EditorCamera.h"

#include <glad/gl.h>

namespace Engine
{

    Scene::Scene()
    {
        // Create default mesh shader (Phong lighting with shadow mapping)
        std::string vertexSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec3 a_Normal;
            layout(location = 2) in vec2 a_TexCoords;

            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;
            uniform mat4 u_LightSpaceMatrix;

            out vec3 v_Normal;
            out vec3 v_FragPos;
            out vec2 v_TexCoord;
            out vec4 v_FragPosLightSpace;

            void main() {
                v_Normal = mat3(transpose(inverse(u_Transform))) * a_Normal;
                v_FragPos = vec3(u_Transform * vec4(a_Position, 1.0));
                v_TexCoord = a_TexCoords;
                v_FragPosLightSpace = u_LightSpaceMatrix * vec4(v_FragPos, 1.0);
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

            // Shadow mapping
            uniform sampler2D u_ShadowMap;
            uniform int u_ShadowEnabled;
            uniform float u_ShadowBias;

            in vec3 v_Normal;
            in vec3 v_FragPos;
            in vec2 v_TexCoord;
            in vec4 v_FragPosLightSpace;

            float CalcShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
            {
                vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
                projCoords = projCoords * 0.5 + 0.5;
                if (projCoords.z > 1.0) return 0.0;

                // Slope-scaled bias: steep surfaces get up to 10x base bias
                float slopeFactor = 1.0 - dot(normal, lightDir);
                float bias = u_ShadowBias + u_ShadowBias * 10.0 * slopeFactor;

                // 3x3 PCF
                float shadow = 0.0;
                vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0);
                for (int x = -1; x <= 1; ++x)
                    for (int y = -1; y <= 1; ++y)
                    {
                        float d = texture(u_ShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
                        shadow += (projCoords.z - bias > d) ? 1.0 : 0.0;
                    }
                return shadow / 9.0;
            }

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

                // Shadow calculation for first directional light
                float shadow = 0.0;
                if (u_ShadowEnabled != 0 && u_NumDirLights > 0)
                {
                    vec3 lightDir = normalize(-u_DirLights[0].direction);
                    shadow = CalcShadow(v_FragPosLightSpace, norm, lightDir);
                }

                // Accumulate lighting from all sources
                vec3 result = vec3(0.0);
                // First directional light with shadow
                if (u_NumDirLights > 0)
                    result += (1.0 - shadow) * CalcDirLight(u_DirLights[0], norm, viewDir, u_Shininess);
                // Remaining directional lights without shadow
                for (int i = 1; i < u_NumDirLights; i++)
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

        // Depth shader for shadow map pass (minimal: only outputs gl_Position)
        std::string depthVertSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;

            uniform mat4 u_LightSpaceMatrix;
            uniform mat4 u_Transform;

            void main() {
                gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
            }
        )";

        std::string depthFragSrc = R"(
            #version 330 core
            void main() {
                // Depth is written automatically
            }
        )";

        m_DepthShader = Shader::Create("DepthShader", depthVertSrc, depthFragSrc);

        // Shadow Map FBO (depth-only, no color attachments)
        FramebufferSpecification shadowSpec;
        shadowSpec.Attachments = {FramebufferTextureFormat::DEPTH_COMPONENT};
        shadowSpec.Width = m_ShadowSettings.MapResolution;
        shadowSpec.Height = m_ShadowSettings.MapResolution;
        m_ShadowMapFBO = Framebuffer::Create(shadowSpec);
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

        int numPointLights = 0;
        int numSpotLights = 0;

        // Collect directional lights, ensuring CastShadows=true goes to index 0
        struct DirLightInfo
        {
            glm::vec3 direction;
            glm::vec3 color;
            float intensity;
            bool castShadows;
        };
        std::vector<DirLightInfo> dirLights;

        auto lightView = m_Registry.view<TransformComponent, LightComponent>();
        for (auto entity : lightView)
        {
            auto [transform, light] = lightView.get<TransformComponent, LightComponent>(entity);
            glm::vec3 forward = glm::normalize(glm::quat(transform.Rotation) * glm::vec3(0.0f, 0.0f, -1.0f));

            switch (light.Type)
            {
            case LightComponent::LightType::Directional:
            {
                if (dirLights.size() < 2)
                    dirLights.push_back({forward, light.Color, light.Intensity, light.CastShadows});
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

        // Sort directional lights: CastShadows=true first (stable for consistent ordering)
        std::stable_sort(dirLights.begin(), dirLights.end(),
                         [](const DirLightInfo& a, const DirLightInfo& b)
                         { return a.castShadows > b.castShadows; });

        // Upload directional lights
        int numDirLights = static_cast<int>(dirLights.size());
        for (int i = 0; i < numDirLights; i++)
        {
            std::string prefix = "u_DirLights[" + std::to_string(i) + "]";
            m_MeshShader->SetFloat3(prefix + ".direction", dirLights[i].direction);
            m_MeshShader->SetFloat3(prefix + ".color", dirLights[i].color);
            m_MeshShader->SetFloat(prefix + ".intensity", dirLights[i].intensity);
        }

        m_MeshShader->SetInt("u_NumDirLights", numDirLights);
        m_MeshShader->SetInt("u_NumPointLights", numPointLights);
        m_MeshShader->SetInt("u_NumSpotLights", numSpotLights);

        // Shadow uniforms
        m_MeshShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrix);
        m_MeshShader->SetInt("u_ShadowEnabled", m_ShadowSettings.Enabled ? 1 : 0);
        m_MeshShader->SetFloat("u_ShadowBias", m_ShadowSettings.Bias);
        // Shadow map on texture unit 1 (unit 0 is diffuse texture)
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_ShadowMapFBO->GetDepthAttachmentRendererID());
        m_MeshShader->SetInt("u_ShadowMap", 1);

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

    void Scene::ShadowPass()
    {
        if (!m_ShadowSettings.Enabled)
            return;

        // Find the first CastShadows directional light
        glm::vec3 lightDir{0.0f};
        bool found = false;

        auto lightView = m_Registry.view<TransformComponent, LightComponent>();
        for (auto entity : lightView)
        {
            auto [transform, light] = lightView.get<TransformComponent, LightComponent>(entity);
            if (light.Type == LightComponent::LightType::Directional && light.CastShadows)
            {
                lightDir = glm::normalize(glm::quat(transform.Rotation) * glm::vec3(0.0f, 0.0f, -1.0f));
                found = true;
                break;
            }
        }

        if (!found)
            return;

        // Compute light space matrix
        float s = m_ShadowSettings.OrthoSize;
        glm::mat4 lightProjection = glm::ortho(-s, s, -s, s, m_ShadowSettings.NearPlane, m_ShadowSettings.FarPlane);

        // Position the light camera looking from far behind the scene center along the light direction
        glm::vec3 lightPos = -lightDir * m_ShadowSettings.OrthoSize;
        // Handle degenerate up vector when light is nearly vertical
        glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                            ? glm::vec3(0.0f, 0.0f, 1.0f)
                            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 lightViewMat = glm::lookAt(lightPos, lightPos + lightDir, up);

        m_LightSpaceMatrix = lightProjection * lightViewMat;

        // Render to shadow map
        m_ShadowMapFBO->Bind();
        RenderCommand::Clear();

        // Front-face culling to reduce shadow acne
        glCullFace(GL_FRONT);

        m_DepthShader->Bind();
        m_DepthShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrix);

        auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView)
        {
            auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);
            if (meshRenderer.MeshData)
            {
                m_DepthShader->SetMat4("u_Transform", transform.GetTransform());
                meshRenderer.MeshData->GetVertexArray()->Bind();
                RenderCommand::DrawIndexed(meshRenderer.MeshData->GetVertexArray());
            }
        }

        // Restore back-face culling
        glCullFace(GL_BACK);

        m_ShadowMapFBO->Unbind();
    }

    void Scene::ResizeShadowMap(int resolution)
    {
        m_ShadowSettings.MapResolution = resolution;
        m_ShadowMapFBO->Resize(resolution, resolution);
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
