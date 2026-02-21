#include "engpch.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shader.h"
#include "Renderer/Mesh.h"
#include "Renderer/EditorCamera.h"

namespace Engine
{

    Scene::Scene()
    {
        // Create default mesh shader (outputs color + entity ID for picking)
        std::string vertexSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec3 a_Normal;
            layout(location = 2) in vec2 a_TexCoords;

            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;

            out vec3 v_Normal;
            out vec3 v_FragPos;

            void main() {
                v_Normal = mat3(transpose(inverse(u_Transform))) * a_Normal;
                v_FragPos = vec3(u_Transform * vec4(a_Position, 1.0));
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
            }
        )";

        std::string fragmentSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 o_Color;
            layout(location = 1) out int o_EntityID;

            uniform vec4 u_Color;
            uniform int u_EntityID;

            in vec3 v_Normal;
            in vec3 v_FragPos;

            void main() {
                // Simple directional light
                vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
                vec3 norm = normalize(v_Normal);
                float diff = max(dot(norm, lightDir), 0.0);
                float ambient = 0.3;
                float lighting = ambient + diff * 0.7;
                o_Color = vec4(u_Color.rgb * lighting, u_Color.a);
                o_EntityID = u_EntityID;
            }
        )";

        m_MeshShader = Shader::Create("MeshShader", vertexSrc, fragmentSrc);
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

        auto view = m_Registry.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : view)
        {
            auto [transform, meshRenderer] = view.get<TransformComponent, MeshRendererComponent>(entity);

            if (meshRenderer.MeshData)
            {
                m_MeshShader->Bind();
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
