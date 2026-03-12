#pragma once

#include "Asset/AssetHandle.h"
#include "Core/Base.h"
#include "Core/UUID.h"
#include "Scene/SceneCamera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <memory>
#include <string>
#include <vector>

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
        IDComponent(UUID id) : ID(id) {}
    };

    struct TagComponent
    {
        std::string Tag = "Entity";

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag) : Tag(tag) {}
    };

    struct TransformComponent
    {
        glm::vec3 Translation = {0.0f, 0.0f, 0.0f};
        glm::vec3 Rotation = {0.0f, 0.0f, 0.0f};
        glm::vec3 Scale = {1.0f, 1.0f, 1.0f};

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::vec3& translation) : Translation(translation) {}

        // 获取本地变换矩阵
        glm::mat4 GetTransform() const
        {
            glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));

            return glm::translate(glm::mat4(1.0f), Translation) * rotation * glm::scale(glm::mat4(1.0f), Scale);
        }
    };

    // 父子层级关系组件
    struct RelationshipComponent
    {
        UUID ParentID = 0;          // 父实体 UUID，0 = 根节点
        std::vector<UUID> Children; // 子实体 UUID 列表

        RelationshipComponent() = default;
        RelationshipComponent(const RelationshipComponent&) = default;
    };

    // MeshType 标识原始类型，用于序列化和 UI
    enum class MeshType
    {
        Cube = 0,
        Plane,
        Sphere,
        Model
    };

    struct MeshRendererComponent
    {
        MeshType Type = MeshType::Cube;
        AssetHandle MeshAsset; // → AssetManager::Get<Mesh>()
        glm::vec4 Color = {1.0f, 1.0f, 1.0f, 1.0f};

        // 纹理句柄（通过 AssetManager 解析）
        AssetHandle DiffuseTextureAsset; // Albedo
        glm::vec2 Tiling = {1.0f, 1.0f};
        float Shininess = 32.0f;

        AssetHandle NormalMapAsset; // 法线贴图

        // PBR 参数
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        AssetHandle MetallicTextureAsset;
        AssetHandle RoughnessTextureAsset;
        AssetHandle AOTextureAsset;

        std::vector<Ref<Material>> CachedMaterials; // 运行时构建，每 SubMesh 一个

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
        enum class BodyType
        {
            Static = 0,
            Dynamic = 1,
            Kinematic = 2
        };
        BodyType Type = BodyType::Static;
        float Mass = 1.0f;
        float Restitution = 0.3f; // 弹性系数 (e)
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
        bool IsTrigger = false;

        BoxColliderComponent() = default;
        BoxColliderComponent(const BoxColliderComponent&) = default;
    };

    struct SphereColliderComponent
    {
        float Radius = 0.5f;
        glm::vec3 Offset = {0, 0, 0};
        bool IsTrigger = false;

        SphereColliderComponent() = default;
        SphereColliderComponent(const SphereColliderComponent&) = default;
    };

    struct MeshColliderComponent
    {
        enum class ColliderType
        {
            Convex = 0,
            Static = 1
        };
        ColliderType Type = ColliderType::Convex;
        std::string MeshPath; // 指定模型路径（空 = 使用 MeshRendererComponent 的网格）
        bool IsTrigger = false;

        MeshColliderComponent() = default;
        MeshColliderComponent(const MeshColliderComponent&) = default;
    };

    struct ParticleEmitterComponent
    {
        // 预设
        enum class Preset
        {
            Custom = 0,
            Fire,
            Smoke,
            Explosion,
            Sparks
        };
        Preset CurrentPreset = Preset::Custom;

        // 发射参数
        float EmitRate = 100.0f; // 粒子/秒
        int BurstCount = 0;      // 一次性爆发数量
        uint32_t MaxParticles = 10000;

        // 初始值范围
        float LifeMin = 1.0f, LifeMax = 3.0f;
        float SpeedMin = 1.0f, SpeedMax = 5.0f;
        float SizeStart = 0.1f, SizeEnd = 0.0f;

        // 发射方向 (锥形)
        glm::vec3 EmitDirection = {0.0f, 1.0f, 0.0f};
        float EmitAngle = 30.0f; // 锥体半角 (度)

        // 颜色
        glm::vec4 ColorStart = {1.0f, 0.8f, 0.2f, 1.0f}; // 火焰橙
        glm::vec4 ColorEnd = {1.0f, 0.0f, 0.0f, 0.0f};   // 红色淡出

        // 物理
        glm::vec3 Gravity = {0.0f, -9.81f, 0.0f};
        float Damping = 0.98f;

        // 混合模式
        enum class BlendMode
        {
            Additive = 0,
            AlphaBlend = 1
        };
        BlendMode Blend = BlendMode::Additive;

        // SPH 流体参数（SPHEnabled=true 时启用）
        bool SPHEnabled = false;
        float SPH_RestDensity = 200.0f;   // 静止密度 ρ_0
        float SPH_GasConstant = 50.0f;    // 气体常数 k（刚度）
        float SPH_Viscosity = 3.5f;       // 粘性系数 μ
        float SPH_SmoothingRadius = 0.1f; // 光滑核半径 h
        float SPH_ParticleMass = 0.02f;   // 单粒子质量 m

        // PCISPH
        bool SPH_PCISPHEnabled = true;
        int SPH_PCISPHIterations = 3; // 1-8
        float SPH_PCISPHDelta = 0.3f;
        // 表面张力
        float SPH_SurfaceTension = 0.0f; // γ, 0=关闭
        // 刚体耦合
        bool SPH_RigidBodyCoupling = false;
        float SPH_BoundaryStiffness = 5000.0f;
        float SPH_BoundaryDamping = 0.5f;

        // 运行时（不序列化）
        int CollisionBurstCount = 0; // 碰撞触发的爆发（帧末自动清零）
        int PendingBurst = 0;        // 本帧待发射的爆发数（帧末自动清零，不序列化）

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
                emitter.PendingBurst = 0;
                emitter.LifeMin = 0.5f;
                emitter.LifeMax = 1.5f;
                emitter.SpeedMin = 1.0f;
                emitter.SpeedMax = 3.0f;
                emitter.SizeStart = 0.1f;
                emitter.SizeEnd = 0.3f;
                emitter.EmitDirection = {0.0f, 1.0f, 0.0f};
                emitter.EmitAngle = 15.0f;
                emitter.ColorStart = {2.0f, 1.2f, 0.2f, 1.0f}; // HDR 橙
                emitter.ColorEnd = {0.5f, 0.05f, 0.0f, 0.0f};  // 暗红淡出
                emitter.Gravity = {0.0f, 0.5f, 0.0f};          // 上浮
                emitter.Damping = 0.96f;
                emitter.Blend = BlendMode::Additive;
                break;
            case Preset::Smoke:
                emitter.EmitRate = 50.0f;
                emitter.BurstCount = 0;
                emitter.PendingBurst = 0;
                emitter.LifeMin = 2.0f;
                emitter.LifeMax = 5.0f;
                emitter.SpeedMin = 0.3f;
                emitter.SpeedMax = 1.0f;
                emitter.SizeStart = 0.2f;
                emitter.SizeEnd = 0.8f;
                emitter.EmitDirection = {0.0f, 1.0f, 0.0f};
                emitter.EmitAngle = 20.0f;
                emitter.ColorStart = {0.5f, 0.5f, 0.5f, 0.6f}; // 灰半透明
                emitter.ColorEnd = {0.3f, 0.3f, 0.3f, 0.0f};   // 透明
                emitter.Gravity = {0.0f, 0.3f, 0.0f};          // 缓慢上浮
                emitter.Damping = 0.99f;
                emitter.Blend = BlendMode::AlphaBlend;
                break;
            case Preset::Explosion:
                emitter.EmitRate = 0.0f;
                emitter.BurstCount = 500;
                emitter.PendingBurst = 500;
                emitter.LifeMin = 0.3f;
                emitter.LifeMax = 1.0f;
                emitter.SpeedMin = 3.0f;
                emitter.SpeedMax = 10.0f;
                emitter.SizeStart = 0.05f;
                emitter.SizeEnd = 0.15f;
                emitter.EmitDirection = {0.0f, 1.0f, 0.0f};
                emitter.EmitAngle = 180.0f;                    // 全方向
                emitter.ColorStart = {4.0f, 3.0f, 1.0f, 1.0f}; // 超亮 HDR
                emitter.ColorEnd = {0.5f, 0.05f, 0.0f, 0.0f};  // 暗红淡出
                emitter.Gravity = {0.0f, -4.0f, 0.0f};
                emitter.Damping = 0.95f;
                emitter.Blend = BlendMode::Additive;
                break;
            case Preset::Sparks:
                emitter.EmitRate = 300.0f;
                emitter.BurstCount = 0;
                emitter.PendingBurst = 0;
                emitter.LifeMin = 0.2f;
                emitter.LifeMax = 0.8f;
                emitter.SpeedMin = 2.0f;
                emitter.SpeedMax = 8.0f;
                emitter.SizeStart = 0.01f;
                emitter.SizeEnd = 0.03f;
                emitter.EmitDirection = {0.0f, 1.0f, 0.0f};
                emitter.EmitAngle = 45.0f;
                emitter.ColorStart = {3.0f, 2.5f, 0.5f, 1.0f}; // 亮黄 HDR
                emitter.ColorEnd = {1.0f, 0.3f, 0.0f, 0.0f};
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
        int BurstOnCollision = 50;      // 碰撞时爆发粒子数
        float MinImpulse = 1.0f;        // 最小触发冲量
        bool UseCollisionNormal = true; // 是否用碰撞法线作为发射方向
        int MaxBurstPerFrame = 500;     // 每帧最大碰撞爆发数

        CollisionParticleTriggerComponent() = default;
        CollisionParticleTriggerComponent(const CollisionParticleTriggerComponent&) = default;
    };

    struct TerrainComponent
    {
        // 高度图
        std::string HeightmapPath;  // 相对路径 PNG (R 通道 0-255→0.0-1.0)
        float HeightScale = 50.0f;  // 高度振幅
        float TerrainSize = 100.0f; // XZ 平面边长（正方形）

        // Splat Map (RGBA → 4 层权重)
        std::string SplatmapPath;

        // 4 层纹理
        AssetHandle LayerTextures[4];            // 漫反射贴图
        AssetHandle LayerNormalMaps[4];          // 法线贴图（可选）
        float LayerTiling[4] = {10, 10, 10, 10}; // 各层 UV 平铺
        float LayerMetallic[4] = {0, 0, 0, 0};
        float LayerRoughness[4] = {0.8f, 0.7f, 0.9f, 0.6f};

        // 物理
        float Friction = 0.8f;
        float Restitution = 0.1f;

        // LOD
        int LODLevels = 3; // 1-3
        float LODDistance1 = 50.0f;
        float LODDistance2 = 100.0f;

        // 草地
        bool GrassEnabled = false;
        float GrassDensity = 5.0f; // 每平方米草片数
        float GrassHeight = 0.4f;
        float GrassWidth = 0.1f;
        float GrassWindStrength = 0.3f;
        AssetHandle GrassTexture;

        // 运行时（不序列化）
        bool MeshDirty = true;

        TerrainComponent() = default;
        TerrainComponent(const TerrainComponent&) = default;
    };

    struct AudioSourceComponent
    {
        std::string AudioPath; // WAV 文件路径
        float Volume = 1.0f;
        float Pitch = 1.0f;
        float MinDistance = 1.0f;  // 3D 衰减最小距离
        float MaxDistance = 50.0f; // 3D 衰减最大距离
        bool Loop = false;
        bool PlayOnStart = true;
        bool Spatial = true; // true=3D空间音效, false=2D

        AudioSourceComponent() = default;
        AudioSourceComponent(const AudioSourceComponent&) = default;
    };

    struct AudioListenerComponent
    {
        bool Active = true; // 场景中只有一个激活的 Listener

        AudioListenerComponent() = default;
        AudioListenerComponent(const AudioListenerComponent&) = default;
    };

    struct VideoPlayerComponent
    {
        std::string StreamURL; // rtmp://... 或本地文件路径
        bool PlayOnStart = true;
        bool Loop = false;   // 仅本地文件有效
        float Volume = 1.0f; // 视频音轨音量

        VideoPlayerComponent() = default;
        VideoPlayerComponent(const VideoPlayerComponent&) = default;
        VideoPlayerComponent& operator=(const VideoPlayerComponent&) = default;
    };

    struct FluidEmitterComponent
    {
        // 发射参数
        uint32_t ParticleCount = 5000;
        float ParticleRadius = 0.02f;
        glm::vec3 EmitExtents = {0.3f, 0.3f, 0.3f};
        glm::vec3 InitialVelocity = {0.0f, 0.0f, 0.0f};

        // SPH 参数
        float RestDensity = 1000.0f;
        float GasConstant = 50.0f;
        float Viscosity = 3.5f;
        float SmoothingRadius = 0.1f;
        float ParticleMass = 0.02f;
        glm::vec3 Gravity = {0.0f, -9.81f, 0.0f};
        float Damping = 0.998f;

        // PCISPH
        bool PCISPHEnabled = true;
        int PCISPHIterations = 3;
        float PCISPHDelta = 0.3f;
        float SurfaceTension = 0.0f;

        // 刚体耦合
        bool RigidBodyCoupling = true;
        float BoundaryStiffness = 5000.0f;
        float BoundaryDamping = 0.5f;

        // 边界盒
        bool UseBoundary = true;
        glm::vec3 BoundaryMin = {-1.0f, -1.0f, -1.0f};
        glm::vec3 BoundaryMax = {1.0f, 1.0f, 1.0f};

        // 渲染参数
        glm::vec3 FluidColor = {0.1f, 0.4f, 0.8f};
        glm::vec3 AbsorptionColor = {0.5f, 0.2f, 0.05f};
        float AbsorptionScale = 5.0f;
        float FresnelPower = 3.0f;
        float RefractionStrength = 0.05f;
        float Reflectivity = 0.04f;
        int SmoothIterations = 2;
        float SmoothFilterRadius = 5.0f;
        float SmoothDepthFalloff = 100.0f;

        FluidEmitterComponent() = default;
        FluidEmitterComponent(const FluidEmitterComponent&) = default;
    };

} // namespace Engine
