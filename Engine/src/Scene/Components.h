#pragma once

#include "Core/Base.h"
#include "Core/UUID.h"
#include "Scene/SceneCamera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <string>

namespace Engine
{

    class Mesh;

    struct IDComponent
    {
        UUID ID;

        IDComponent() = default;
        IDComponent(const IDComponent&) = default;
        IDComponent(UUID id)
            : ID(id)
        {
        }
    };

    struct TagComponent
    {
        std::string Tag = "Entity";

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag)
            : Tag(tag)
        {
        }
    };

    struct TransformComponent
    {
        glm::vec3 Translation = {0.0f, 0.0f, 0.0f};
        glm::vec3 Rotation = {0.0f, 0.0f, 0.0f};
        glm::vec3 Scale = {1.0f, 1.0f, 1.0f};

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::vec3& translation)
            : Translation(translation)
        {
        }

        glm::mat4 GetTransform() const
        {
            glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));

            return glm::translate(glm::mat4(1.0f), Translation) * rotation * glm::scale(glm::mat4(1.0f), Scale);
        }
    };

    struct MeshRendererComponent
    {
        Ref<Mesh> MeshData;
        glm::vec4 Color = {1.0f, 1.0f, 1.0f, 1.0f};

        MeshRendererComponent() = default;
        MeshRendererComponent(const MeshRendererComponent&) = default;
    };

    struct CameraComponent
    {
        SceneCamera Camera;
        bool Primary = true;
        bool FixedAspectRatio = false;

        CameraComponent() = default;
        CameraComponent(const CameraComponent&) = default;
    };

    struct LightComponent
    {
        glm::vec3 Color = {1.0f, 1.0f, 1.0f};
        float Intensity = 1.0f;

        LightComponent() = default;
        LightComponent(const LightComponent&) = default;
    };

} // namespace Engine
