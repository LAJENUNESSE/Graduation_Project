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
    class Texture2D;

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

        // 材质属性
        Ref<Texture2D> DiffuseTexture;        // null = 纯色
        std::string TexturePath;              // 用于序列化
        glm::vec2 Tiling = {1.0f, 1.0f};     // 纹理平铺
        float Shininess = 32.0f;              // 高光指数

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
        enum class LightType : int
        {
            Directional = 0,
            Point = 1,
            Spot = 2
        };

        LightType Type = LightType::Directional;
        glm::vec3 Color = {1.0f, 1.0f, 1.0f};
        float Intensity = 1.0f;

        // 衰减参数（Point + Spot）
        float Constant = 1.0f;
        float Linear = 0.09f;
        float Quadratic = 0.032f;

        // 锥角（Spot only），弧度制
        float InnerCutoff = glm::radians(12.5f);
        float OuterCutoff = glm::radians(17.5f);

        LightComponent() = default;
        LightComponent(const LightComponent&) = default;
    };

} // namespace Engine
