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
    class Material;

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
        Ref<Texture2D> DiffuseTexture;        // null = 纯色 (Albedo)
        std::string TexturePath;              // 用于序列化
        glm::vec2 Tiling = {1.0f, 1.0f};     // 纹理平铺
        float Shininess = 32.0f;              // 高光指数 (Phong fallback, unused in PBR)

        // 法线贴图
        Ref<Texture2D> NormalMapTexture;      // null = 不使用法线贴图
        std::string NormalMapPath;            // 用于序列化

        // PBR 参数 (Metallic-Roughness 工作流)
        float Metallic = 0.0f;                // 金属度 0-1
        float Roughness = 0.5f;               // 粗糙度 0-1
        Ref<Texture2D> MetallicTexture;       // 可选金属度贴图
        std::string MetallicTexturePath;
        Ref<Texture2D> RoughnessTexture;      // 可选粗糙度贴图
        std::string RoughnessTexturePath;
        Ref<Texture2D> AOTexture;             // 可选环境遮蔽贴图
        std::string AOTexturePath;

        std::string ModelPath;                // 模型文件相对路径（Model 类型时非空）

        Ref<Material> MaterialInstance;       // 可选，运行时构建的 Material 实例

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

        // 阴影（仅方向光有效）
        bool CastShadows = true;

        LightComponent() = default;
        LightComponent(const LightComponent&) = default;
    };

    struct RigidBodyComponent
    {
        enum class BodyType { Static = 0, Dynamic = 1, Kinematic = 2 };
        BodyType Type = BodyType::Static;
        float Mass = 1.0f;
        float Restitution = 0.3f;    // 弹性系数 (e)
        float Friction = 0.5f;
        float GravityScale = 1.0f;
        bool FixedRotation = false;

        // 运行时状态（不序列化）
        glm::vec3 LinearVelocity = {0, 0, 0};
        glm::vec3 AngularVelocity = {0, 0, 0};
        glm::vec3 Force = {0, 0, 0};
        glm::vec3 Torque = {0, 0, 0};

        RigidBodyComponent() = default;
        RigidBodyComponent(const RigidBodyComponent&) = default;
    };

    struct BoxColliderComponent
    {
        glm::vec3 HalfExtents = {0.5f, 0.5f, 0.5f};
        glm::vec3 Offset = {0, 0, 0};

        BoxColliderComponent() = default;
        BoxColliderComponent(const BoxColliderComponent&) = default;
    };

    struct SphereColliderComponent
    {
        float Radius = 0.5f;
        glm::vec3 Offset = {0, 0, 0};

        SphereColliderComponent() = default;
        SphereColliderComponent(const SphereColliderComponent&) = default;
    };

} // namespace Engine
