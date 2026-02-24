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
#include "Renderer/VertexArray.h"
#include "Renderer/Buffer.h"
#include "Renderer/EditorCamera.h"
#include "Debug/PerformanceMonitor.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/BulletPhysicsWorld.h"

namespace Engine
{

    Scene::Scene()
    {
        // Create default mesh shader (PBR Cook-Torrance BRDF with shadow mapping)
        std::string vertexSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec3 a_Normal;
            layout(location = 2) in vec2 a_TexCoords;
            layout(location = 3) in vec3 a_Tangent;

            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;
            uniform mat4 u_LightSpaceMatrix;

            out vec3 v_Normal;
            out vec3 v_FragPos;
            out vec2 v_TexCoord;
            out vec4 v_FragPosLightSpace;
            out mat3 v_TBN;

            void main() {
                mat3 normalMatrix = mat3(transpose(inverse(u_Transform)));
                v_Normal = normalMatrix * a_Normal;
                v_FragPos = vec3(u_Transform * vec4(a_Position, 1.0));
                v_TexCoord = a_TexCoords;
                v_FragPosLightSpace = u_LightSpaceMatrix * vec4(v_FragPos, 1.0);

                // TBN matrix for normal mapping
                vec3 T = normalize(normalMatrix * a_Tangent);
                vec3 N = normalize(v_Normal);
                T = normalize(T - dot(T, N) * N);  // Gram-Schmidt orthogonalization
                vec3 B = cross(N, T);
                v_TBN = mat3(T, B, N);

                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
            }
        )";

        std::string fragmentSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 o_Color;
            layout(location = 1) out int o_EntityID;

            const float PI = 3.14159265359;

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

            // Textures
            uniform sampler2D u_DiffuseTexture;  // unit 0 - Albedo
            uniform int u_HasTexture;
            uniform vec2 u_Tiling;

            // PBR parameters
            uniform float u_Metallic;
            uniform float u_Roughness;
            uniform sampler2D u_MetallicMap;    // unit 3
            uniform sampler2D u_RoughnessMap;   // unit 4
            uniform sampler2D u_AOMap;           // unit 5
            uniform int u_HasMetallicMap;
            uniform int u_HasRoughnessMap;
            uniform int u_HasAOMap;

            uniform int u_NumDirLights;
            uniform int u_NumPointLights;
            uniform int u_NumSpotLights;

            uniform DirLight   u_DirLights[MAX_DIR_LIGHTS];
            uniform PointLight u_PointLights[MAX_POINT_LIGHTS];
            uniform SpotLight  u_SpotLights[MAX_SPOT_LIGHTS];

            // Shadow mapping
            uniform sampler2D u_ShadowMap;       // unit 1
            uniform int u_ShadowEnabled;
            uniform float u_ShadowBias;

            // Normal mapping
            uniform sampler2D u_NormalMap;        // unit 2
            uniform int u_HasNormalMap;

            in vec3 v_Normal;
            in vec3 v_FragPos;
            in vec2 v_TexCoord;
            in vec4 v_FragPosLightSpace;
            in mat3 v_TBN;

            // ---- PBR Functions ----

            // GGX/Trowbridge-Reitz Normal Distribution
            float DistributionGGX(vec3 N, vec3 H, float roughness)
            {
                float a = roughness * roughness;
                float a2 = a * a;
                float NdotH = max(dot(N, H), 0.0);
                float NdotH2 = NdotH * NdotH;
                float denom = NdotH2 * (a2 - 1.0) + 1.0;
                denom = PI * denom * denom;
                return a2 / max(denom, 0.0001);
            }

            // Schlick-GGX Geometry (single direction)
            float GeometrySchlickGGX(float NdotV, float roughness)
            {
                float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
                return NdotV / (NdotV * (1.0 - k) + k);
            }

            // Smith's Geometry (both directions)
            float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
            {
                float NdotV = max(dot(N, V), 0.0);
                float NdotL = max(dot(N, L), 0.0);
                float ggx1 = GeometrySchlickGGX(NdotV, roughness);
                float ggx2 = GeometrySchlickGGX(NdotL, roughness);
                return ggx1 * ggx2;
            }

            // Fresnel-Schlick approximation
            vec3 FresnelSchlick(float cosTheta, vec3 F0)
            {
                return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
            }

            // ---- Shadow Function ----

            float CalcShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
            {
                vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
                projCoords = projCoords * 0.5 + 0.5;
                if (projCoords.z > 1.0) return 0.0;

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

            // ---- Per-light PBR radiance ----

            vec3 CalcPBRLight(vec3 L, vec3 radiance, vec3 N, vec3 V,
                              vec3 albedo, float metallic, float roughness, vec3 F0)
            {
                vec3 H = normalize(V + L);
                float NdotL = max(dot(N, L), 0.0);

                // Cook-Torrance specular BRDF
                float D = DistributionGGX(N, H, roughness);
                vec3  F = FresnelSchlick(max(dot(H, V), 0.0), F0);
                float G = GeometrySmith(N, V, L, roughness);

                vec3 numerator = D * F * G;
                float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.001;
                vec3 specular = numerator / denominator;

                // Energy conservation: diffuse = (1 - specular) * (1 - metallic)
                vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

                return (kD * albedo + specular) * radiance * NdotL;
            }

            void main() {
                // Normal: use normal map if available, otherwise vertex normal
                vec3 N;
                if (u_HasNormalMap != 0) {
                    N = texture(u_NormalMap, v_TexCoord * u_Tiling).rgb;
                    N = N * 2.0 - 1.0;
                    N = normalize(v_TBN * N);
                } else {
                    N = normalize(v_Normal);
                }
                vec3 V = normalize(u_ViewPos - v_FragPos);

                // Albedo (sRGB → Linear)
                vec4 texColor = (u_HasTexture != 0) ? texture(u_DiffuseTexture, v_TexCoord * u_Tiling) : vec4(1.0);
                if (u_HasTexture != 0)
                    texColor.rgb = pow(texColor.rgb, vec3(2.2));
                vec3 albedo = texColor.rgb * u_Color.rgb;

                // PBR parameters (uniform or texture)
                float metallic = u_Metallic;
                if (u_HasMetallicMap != 0)
                    metallic = texture(u_MetallicMap, v_TexCoord * u_Tiling).r;

                float roughness = u_Roughness;
                if (u_HasRoughnessMap != 0)
                    roughness = texture(u_RoughnessMap, v_TexCoord * u_Tiling).r;

                float ao = 1.0;
                if (u_HasAOMap != 0)
                    ao = texture(u_AOMap, v_TexCoord * u_Tiling).r;

                // F0: dielectric = 0.04, metallic = albedo
                vec3 F0 = mix(vec3(0.04), albedo, metallic);

                // Shadow calculation for first directional light
                float shadow = 0.0;
                if (u_ShadowEnabled != 0 && u_NumDirLights > 0)
                {
                    vec3 lightDir = normalize(-u_DirLights[0].direction);
                    shadow = CalcShadow(v_FragPosLightSpace, N, lightDir);
                }

                // Accumulate lighting
                vec3 Lo = vec3(0.0);

                // Directional lights
                if (u_NumDirLights > 0) {
                    vec3 L = normalize(-u_DirLights[0].direction);
                    vec3 radiance = u_DirLights[0].color * u_DirLights[0].intensity;
                    Lo += (1.0 - shadow) * CalcPBRLight(L, radiance, N, V, albedo, metallic, roughness, F0);
                }
                for (int i = 1; i < u_NumDirLights; i++) {
                    vec3 L = normalize(-u_DirLights[i].direction);
                    vec3 radiance = u_DirLights[i].color * u_DirLights[i].intensity;
                    Lo += CalcPBRLight(L, radiance, N, V, albedo, metallic, roughness, F0);
                }

                // Point lights
                for (int i = 0; i < u_NumPointLights; i++) {
                    vec3 L = normalize(u_PointLights[i].position - v_FragPos);
                    float distance = length(u_PointLights[i].position - v_FragPos);
                    float attenuation = 1.0 / (u_PointLights[i].constant + u_PointLights[i].linear * distance + u_PointLights[i].quadratic * distance * distance);
                    vec3 radiance = u_PointLights[i].color * u_PointLights[i].intensity * attenuation;
                    Lo += CalcPBRLight(L, radiance, N, V, albedo, metallic, roughness, F0);
                }

                // Spot lights
                for (int i = 0; i < u_NumSpotLights; i++) {
                    vec3 L = normalize(u_SpotLights[i].position - v_FragPos);
                    float distance = length(u_SpotLights[i].position - v_FragPos);
                    float attenuation = 1.0 / (u_SpotLights[i].constant + u_SpotLights[i].linear * distance + u_SpotLights[i].quadratic * distance * distance);
                    float theta = dot(L, normalize(-u_SpotLights[i].direction));
                    float epsilon = u_SpotLights[i].innerCutoff - u_SpotLights[i].outerCutoff;
                    float spotIntensity = (abs(epsilon) < 0.0001) ? step(u_SpotLights[i].innerCutoff, theta) : clamp((theta - u_SpotLights[i].outerCutoff) / epsilon, 0.0, 1.0);
                    vec3 radiance = u_SpotLights[i].color * u_SpotLights[i].intensity * attenuation * spotIntensity;
                    Lo += CalcPBRLight(L, radiance, N, V, albedo, metallic, roughness, F0);
                }

                // Ambient (simple, non-IBL)
                vec3 ambient = vec3(u_AmbientStrength) * albedo * ao;

                // Output linear HDR (tone mapping + gamma done in post-processing)
                vec3 color = ambient + Lo;
                o_Color = vec4(color, u_Color.a * texColor.a);
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

        // Skybox shader
        std::string skyboxVertSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;

            uniform mat4 u_ViewProjection;

            out vec3 v_TexCoords;

            void main() {
                v_TexCoords = a_Position;
                vec4 pos = u_ViewProjection * vec4(a_Position, 1.0);
                gl_Position = pos.xyww;
            }
        )";

        std::string skyboxFragSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 o_Color;
            layout(location = 1) out int o_EntityID;

            in vec3 v_TexCoords;
            uniform samplerCube u_Skybox;

            void main() {
                vec4 texColor = texture(u_Skybox, v_TexCoords);
                texColor.rgb = pow(texColor.rgb, vec3(2.2));  // sRGB -> Linear
                o_Color = texColor;
                o_EntityID = -1;
            }
        )";

        m_SkyboxShader = Shader::Create("SkyboxShader", skyboxVertSrc, skyboxFragSrc);

        // Skybox cube VAO (36 vertices, positions only)
        float skyboxVertices[] = {
            -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f,
        };

        auto skyboxVB = VertexBuffer::Create(skyboxVertices, sizeof(skyboxVertices));
        skyboxVB->SetLayout({{ShaderDataType::Float3, "a_Position"}});

        m_SkyboxVAO = VertexArray::Create();
        m_SkyboxVAO->AddVertexBuffer(skyboxVB);

        // No index buffer — 36 vertices drawn as GL_TRIANGLES via glDrawArrays
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
        // 运行时：同步移除物理刚体
        if (m_BulletPhysicsWorld)
            m_BulletPhysicsWorld->DestroyBody((entt::entity)entity);

        m_Registry.destroy(entity);
    }

    void Scene::OnUpdateEditor(Timestep ts, EditorCamera& camera)
    {
        RenderScene(ts, camera);
    }

    void Scene::OnUpdateRuntime(Timestep ts, EditorCamera& camera)
    {
        // Physics step
        if (m_PhysicsBackend == PhysicsBackend::Custom && m_PhysicsWorld)
            m_PhysicsWorld->Step(ts, m_Registry);
        else if (m_PhysicsBackend == PhysicsBackend::Bullet && m_BulletPhysicsWorld)
            m_BulletPhysicsWorld->Step(ts, m_Registry);

        // Render
        RenderScene(ts, camera);
    }

    void Scene::OnRuntimeStart()
    {
        if (m_PhysicsBackend == PhysicsBackend::Custom)
        {
            m_PhysicsWorld = std::make_unique<PhysicsWorld>();
            m_PhysicsWorld->Init();
        }
        else
        {
            m_BulletPhysicsWorld = std::make_unique<BulletPhysicsWorld>();
            m_BulletPhysicsWorld->Init();
            m_BulletPhysicsWorld->CreateBodies(m_Registry);
        }
    }

    void Scene::OnRuntimeStop()
    {
        m_PhysicsWorld.reset();
        if (m_BulletPhysicsWorld)
        {
            m_BulletPhysicsWorld->Shutdown();
            m_BulletPhysicsWorld.reset();
        }
    }

    Ref<Scene> Scene::Copy(Ref<Scene> src)
    {
        auto newScene = CreateRef<Scene>();

        newScene->m_ViewportWidth = src->m_ViewportWidth;
        newScene->m_ViewportHeight = src->m_ViewportHeight;
        newScene->m_ShadowSettings = src->m_ShadowSettings;
        newScene->m_PhysicsBackend = src->m_PhysicsBackend;

        // Copy skybox
        if (!src->m_SkyboxFacePaths.empty())
            newScene->LoadSkybox(src->m_SkyboxFacePaths);

        // Resize shadow map to match source
        newScene->ResizeShadowMap(src->m_ShadowSettings.MapResolution);

        // Copy all entities
        auto& srcReg = src->m_Registry;
        auto view = srcReg.view<IDComponent>();
        for (auto srcEntity : view)
        {
            UUID uuid = srcReg.get<IDComponent>(srcEntity).ID;
            const auto& name = srcReg.get<TagComponent>(srcEntity).Tag;
            Entity newEntity = newScene->CreateEntityWithUUID(uuid, name);

            // TransformComponent (already added by CreateEntityWithUUID)
            if (srcReg.all_of<TransformComponent>(srcEntity))
            {
                newEntity.GetComponent<TransformComponent>() = srcReg.get<TransformComponent>(srcEntity);
            }

            // MeshRendererComponent
            if (srcReg.all_of<MeshRendererComponent>(srcEntity))
            {
                newEntity.AddComponent<MeshRendererComponent>(srcReg.get<MeshRendererComponent>(srcEntity));
            }

            // LightComponent
            if (srcReg.all_of<LightComponent>(srcEntity))
            {
                newEntity.AddComponent<LightComponent>(srcReg.get<LightComponent>(srcEntity));
            }

            // CameraComponent
            if (srcReg.all_of<CameraComponent>(srcEntity))
            {
                newEntity.AddComponent<CameraComponent>(srcReg.get<CameraComponent>(srcEntity));
            }

            // RigidBodyComponent
            if (srcReg.all_of<RigidBodyComponent>(srcEntity))
            {
                newEntity.AddComponent<RigidBodyComponent>(srcReg.get<RigidBodyComponent>(srcEntity));
            }

            // BoxColliderComponent
            if (srcReg.all_of<BoxColliderComponent>(srcEntity))
            {
                newEntity.AddComponent<BoxColliderComponent>(srcReg.get<BoxColliderComponent>(srcEntity));
            }

            // SphereColliderComponent
            if (srcReg.all_of<SphereColliderComponent>(srcEntity))
            {
                newEntity.AddComponent<SphereColliderComponent>(srcReg.get<SphereColliderComponent>(srcEntity));
            }
        }

        return newScene;
    }

    void Scene::RenderScene(Timestep ts, EditorCamera& camera)
    {
        PerformanceMonitor::Get().GetSceneRenderGPUTimer().Begin();

        Renderer::BeginScene(camera.GetViewProjection());

        // Collect and upload lights
        m_MeshShader->Bind();
        m_MeshShader->SetFloat3("u_ViewPos", camera.GetPosition());
        m_MeshShader->SetFloat("u_AmbientStrength", 0.3f);

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

        // Shadow uniforms — only enable sampling when a valid shadow caster produced this frame's map
        m_MeshShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrix);
        bool shadowActive = m_ShadowSettings.Enabled && m_HasValidShadowCaster;
        m_MeshShader->SetInt("u_ShadowEnabled", shadowActive ? 1 : 0);
        m_MeshShader->SetFloat("u_ShadowBias", m_ShadowSettings.Bias);
        // Shadow map on texture unit 1 (unit 0 is diffuse texture)
        RenderCommand::BindTextureUnit(1, m_ShadowMapFBO->GetDepthAttachmentRendererID());
        m_MeshShader->SetInt("u_ShadowMap", 1);

        // Render meshes
        auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView)
        {
            auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);

            if (meshRenderer.MeshData)
            {
                m_MeshShader->SetFloat4("u_Color", meshRenderer.Color);
                m_MeshShader->SetInt("u_EntityID", static_cast<int>(entity));
                m_MeshShader->SetFloat2("u_Tiling", meshRenderer.Tiling);

                // PBR parameters
                m_MeshShader->SetFloat("u_Metallic", meshRenderer.Metallic);
                m_MeshShader->SetFloat("u_Roughness", meshRenderer.Roughness);

                for (const auto& subMesh : meshRenderer.MeshData->GetSubMeshes())
                {
                    // Per-submesh texture > component texture > white fallback
                    Ref<Texture2D> tex = subMesh.DiffuseTexture ? subMesh.DiffuseTexture : meshRenderer.DiffuseTexture;
                    if (tex)
                    {
                        tex->Bind(0);
                        m_MeshShader->SetInt("u_HasTexture", 1);
                    }
                    else
                    {
                        m_WhiteTexture->Bind(0);
                        m_MeshShader->SetInt("u_HasTexture", 0);
                    }
                    m_MeshShader->SetInt("u_DiffuseTexture", 0);

                    // Normal map: per-submesh > component-level > none
                    Ref<Texture2D> normalTex = subMesh.NormalTexture ? subMesh.NormalTexture : meshRenderer.NormalMapTexture;
                    if (normalTex)
                    {
                        normalTex->Bind(2);
                        m_MeshShader->SetInt("u_HasNormalMap", 1);
                    }
                    else
                    {
                        m_MeshShader->SetInt("u_HasNormalMap", 0);
                    }
                    m_MeshShader->SetInt("u_NormalMap", 2);

                    // Metallic map (unit 3)
                    if (meshRenderer.MetallicTexture)
                    {
                        meshRenderer.MetallicTexture->Bind(3);
                        m_MeshShader->SetInt("u_HasMetallicMap", 1);
                    }
                    else
                    {
                        m_MeshShader->SetInt("u_HasMetallicMap", 0);
                    }
                    m_MeshShader->SetInt("u_MetallicMap", 3);

                    // Roughness map (unit 4)
                    if (meshRenderer.RoughnessTexture)
                    {
                        meshRenderer.RoughnessTexture->Bind(4);
                        m_MeshShader->SetInt("u_HasRoughnessMap", 1);
                    }
                    else
                    {
                        m_MeshShader->SetInt("u_HasRoughnessMap", 0);
                    }
                    m_MeshShader->SetInt("u_RoughnessMap", 4);

                    // AO map (unit 5)
                    if (meshRenderer.AOTexture)
                    {
                        meshRenderer.AOTexture->Bind(5);
                        m_MeshShader->SetInt("u_HasAOMap", 1);
                    }
                    else
                    {
                        m_MeshShader->SetInt("u_HasAOMap", 0);
                    }
                    m_MeshShader->SetInt("u_AOMap", 5);

                    Renderer::Submit(m_MeshShader, subMesh.VAO, transform.GetTransform());
                }
            }
        }

        // Skybox — render last with depth trick (z = 1.0, GL_LEQUAL)
        if (m_SkyboxTexture)
        {
            RenderCommand::SetDepthFunc(DepthFunc::LEqual);

            m_SkyboxShader->Bind();
            // Strip translation from view matrix so skybox doesn't move with camera
            glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(camera.GetViewMatrix()));
            m_SkyboxShader->SetMat4("u_ViewProjection", camera.GetProjection() * viewNoTranslation);
            m_SkyboxShader->SetInt("u_Skybox", 0);
            m_SkyboxTexture->Bind(0);

            m_SkyboxVAO->Bind();
            RenderCommand::DrawArrays(36);

            RenderCommand::SetDepthFunc(DepthFunc::Less);
        }

        Renderer::EndScene();

        PerformanceMonitor::Get().GetSceneRenderGPUTimer().End();
    }

    void Scene::ShadowPass()
    {
        if (!m_ShadowSettings.Enabled)
        {
            // Still need to cycle the GPU timer to avoid stale state
            PerformanceMonitor::Get().GetShadowPassGPUTimer().Begin();
            PerformanceMonitor::Get().GetShadowPassGPUTimer().End();
            m_HasValidShadowCaster = false;
            return;
        }

        PerformanceMonitor::Get().GetShadowPassGPUTimer().Begin();

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
        {
            PerformanceMonitor::Get().GetShadowPassGPUTimer().End();
            m_HasValidShadowCaster = false;
            return;
        }

        m_HasValidShadowCaster = true;

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
        RenderCommand::SetCullFaceMode(CullFaceMode::Front);

        m_DepthShader->Bind();
        m_DepthShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrix);

        auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent>();
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

        // Restore back-face culling
        RenderCommand::SetCullFaceMode(CullFaceMode::Back);

        m_ShadowMapFBO->Unbind();

        PerformanceMonitor::Get().GetShadowPassGPUTimer().End();
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

    void Scene::LoadSkybox(const std::vector<std::string>& facePaths)
    {
        if (facePaths.size() != 6)
        {
            ENGINE_CORE_ERROR("Skybox requires exactly 6 face paths");
            return;
        }
        m_SkyboxFacePaths = facePaths;
        m_SkyboxTexture = TextureCubemap::Create(facePaths);
    }

    void Scene::ClearSkybox()
    {
        m_SkyboxTexture.reset();
        m_SkyboxFacePaths.clear();
    }

} // namespace Engine
