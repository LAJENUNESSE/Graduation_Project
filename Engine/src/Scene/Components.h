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

    struct ParticleEmitterComponent
    {
        // 预设
        enum class Preset { Custom = 0, Fire, Smoke, Explosion, Sparks };
        Preset CurrentPreset = Preset::Custom;

        // 发射参数
        float EmitRate = 100.0f;               // 粒子/秒
        int BurstCount = 0;                    // 一次性爆发数量
        uint32_t MaxParticles = 10000;

        // 初始值范围
        float LifeMin = 1.0f, LifeMax = 3.0f;
        float SpeedMin = 1.0f, SpeedMax = 5.0f;
        float SizeStart = 0.1f, SizeEnd = 0.0f;

        // 发射方向 (锥形)
        glm::vec3 EmitDirection = {0.0f, 1.0f, 0.0f};
        float EmitAngle = 30.0f;               // 锥体半角 (度)

        // 颜色
        glm::vec4 ColorStart = {1.0f, 0.8f, 0.2f, 1.0f};   // 火焰橙
        glm::vec4 ColorEnd = {1.0f, 0.0f, 0.0f, 0.0f};     // 红色淡出

        // 物理
        glm::vec3 Gravity = {0.0f, -9.81f, 0.0f};
        float Damping = 0.98f;

        // 混合模式
        enum class BlendMode { Additive = 0, AlphaBlend = 1 };
        BlendMode Blend = BlendMode::Additive;

        // SPH 流体参数（SPHEnabled=true 时启用）
        bool SPHEnabled = false;
        float SPH_RestDensity = 200.0f;        // 静止密度 ρ_0
        float SPH_GasConstant = 50.0f;         // 气体常数 k（刚度）
        float SPH_Viscosity = 3.5f;            // 粘性系数 μ
        float SPH_SmoothingRadius = 0.1f;      // 光滑核半径 h
        float SPH_ParticleMass = 0.02f;        // 单粒子质量 m

        // 运行时（不序列化）
        void* RuntimeParticleSystem = nullptr;  // ParticleSystemGPU*
        int CollisionBurstCount = 0;            // 碰撞触发的爆发（帧末自动清零）

        ParticleEmitterComponent() = default;
        ParticleEmitterComponent(const ParticleEmitterComponent&) = default;

        // 预设应用
        static void ApplyPreset(ParticleEmitterComponent& emitter, Preset preset)
        {
            emitter.CurrentPreset = preset;
            emitter.SPHEnabled = false;

            switch (preset)
            {
            case Preset::Fire:
                emitter.EmitRate = 200.0f;
                emitter.BurstCount = 0;
                emitter.LifeMin = 0.5f;  emitter.LifeMax = 1.5f;
                emitter.SpeedMin = 1.0f; emitter.SpeedMax = 3.0f;
                emitter.SizeStart = 0.1f; emitter.SizeEnd = 0.3f;
                emitter.EmitDirection = {0.0f, 1.0f, 0.0f};
                emitter.EmitAngle = 15.0f;
                emitter.ColorStart = {2.0f, 1.2f, 0.2f, 1.0f};  // HDR 橙
                emitter.ColorEnd   = {0.5f, 0.05f, 0.0f, 0.0f}; // 暗红淡出
                emitter.Gravity = {0.0f, 0.5f, 0.0f};            // 上浮
                emitter.Damping = 0.96f;
                emitter.Blend = BlendMode::Additive;
                break;
            case Preset::Smoke:
                emitter.EmitRate = 50.0f;
                emitter.BurstCount = 0;
                emitter.LifeMin = 2.0f;  emitter.LifeMax = 5.0f;
                emitter.SpeedMin = 0.3f; emitter.SpeedMax = 1.0f;
                emitter.SizeStart = 0.2f; emitter.SizeEnd = 0.8f;
                emitter.EmitDirection = {0.0f, 1.0f, 0.0f};
                emitter.EmitAngle = 20.0f;
                emitter.ColorStart = {0.5f, 0.5f, 0.5f, 0.6f};  // 灰半透明
                emitter.ColorEnd   = {0.3f, 0.3f, 0.3f, 0.0f};  // 透明
                emitter.Gravity = {0.0f, 0.3f, 0.0f};            // 缓慢上浮
                emitter.Damping = 0.99f;
                emitter.Blend = BlendMode::AlphaBlend;
                break;
            case Preset::Explosion:
                emitter.EmitRate = 0.0f;
                emitter.BurstCount = 500;
                emitter.LifeMin = 0.3f;  emitter.LifeMax = 1.0f;
                emitter.SpeedMin = 3.0f; emitter.SpeedMax = 10.0f;
                emitter.SizeStart = 0.05f; emitter.SizeEnd = 0.15f;
                emitter.EmitDirection = {0.0f, 1.0f, 0.0f};
                emitter.EmitAngle = 180.0f;                       // 全方向
                emitter.ColorStart = {4.0f, 3.0f, 1.0f, 1.0f};  // 超亮 HDR
                emitter.ColorEnd   = {0.5f, 0.05f, 0.0f, 0.0f}; // 暗红淡出
                emitter.Gravity = {0.0f, -4.0f, 0.0f};
                emitter.Damping = 0.95f;
                emitter.Blend = BlendMode::Additive;
                break;
            case Preset::Sparks:
                emitter.EmitRate = 300.0f;
                emitter.BurstCount = 0;
                emitter.LifeMin = 0.2f;  emitter.LifeMax = 0.8f;
                emitter.SpeedMin = 2.0f; emitter.SpeedMax = 8.0f;
                emitter.SizeStart = 0.01f; emitter.SizeEnd = 0.03f;
                emitter.EmitDirection = {0.0f, 1.0f, 0.0f};
                emitter.EmitAngle = 45.0f;
                emitter.ColorStart = {3.0f, 2.5f, 0.5f, 1.0f};  // 亮黄 HDR
                emitter.ColorEnd   = {1.0f, 0.3f, 0.0f, 0.0f};
                emitter.Gravity = {0.0f, -9.81f, 0.0f};
                emitter.Damping = 0.97f;
                emitter.Blend = BlendMode::Additive;
                break;
            case Preset::Custom:
            default:
                break;
            }
        }
    };

    struct CollisionParticleTriggerComponent
    {
        bool Enabled = true;
        int BurstOnCollision = 50;           // 碰撞时爆发粒子数
        float MinImpulse = 1.0f;             // 最小触发冲量
        bool UseCollisionNormal = true;      // 是否用碰撞法线作为发射方向

        CollisionParticleTriggerComponent() = default;
        CollisionParticleTriggerComponent(const CollisionParticleTriggerComponent&) = default;
    };

} // namespace Engine
