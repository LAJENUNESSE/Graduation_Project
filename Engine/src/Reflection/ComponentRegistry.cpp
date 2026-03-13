#include "engpch.h"
#include "Reflection/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Script/NativeScriptComponent.h"

#include <entt/entt.hpp>

namespace Engine
{

    // ---- BoxColliderComponent 反射 ----

    ENGINE_COMPONENT(BoxColliderComponent, "盒碰撞器")
    ENGINE_PROPERTY(BoxColliderComponent, HalfExtents, "半尺寸", Vec3)
    ENGINE_PROPERTY(BoxColliderComponent, Offset, "偏移", Vec3)
    ENGINE_PROPERTY(BoxColliderComponent, IsTrigger, "触发器", Bool)

    REGISTER_COMPONENT_BEGIN(BoxColliderComponent)
    REGISTER_COMPONENT_PROPERTY(BoxColliderComponent, HalfExtents)
    REGISTER_COMPONENT_PROPERTY(BoxColliderComponent, Offset)
    REGISTER_COMPONENT_PROPERTY(BoxColliderComponent, IsTrigger)
    REGISTER_COMPONENT_END(BoxColliderComponent)

    // ---- SphereColliderComponent 反射 ----

    ENGINE_COMPONENT(SphereColliderComponent, "球碰撞器")
    ENGINE_PROPERTY_EX(SphereColliderComponent, Radius, "半径", Float, hints.Speed = 0.01f; hints.Min = 0.01f;
                       hints.Max = 100.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY(SphereColliderComponent, Offset, "偏移", Vec3)
    ENGINE_PROPERTY(SphereColliderComponent, IsTrigger, "触发器", Bool)

    REGISTER_COMPONENT_BEGIN(SphereColliderComponent)
    REGISTER_COMPONENT_PROPERTY(SphereColliderComponent, Radius)
    REGISTER_COMPONENT_PROPERTY(SphereColliderComponent, Offset)
    REGISTER_COMPONENT_PROPERTY(SphereColliderComponent, IsTrigger)
    REGISTER_COMPONENT_END(SphereColliderComponent)

    // ---- MeshColliderComponent 反射 ----

    static const char* s_MeshColliderTypeNames[] = {"凸包", "静态三角网格"};

    ENGINE_COMPONENT(MeshColliderComponent, "网格碰撞器")
    ENGINE_PROPERTY_EX(MeshColliderComponent, Type, "碰撞类型", Enum, hints.EnumNames = s_MeshColliderTypeNames;
                       hints.EnumCount = 2)
    ENGINE_PROPERTY_EX(MeshColliderComponent, MeshPath, "网格路径", AssetPath,
                       hints.FileFilter = "*.gltf;*.glb;*.obj;*.fbx";
                       hints.FileDesc = "3D 模型")
    ENGINE_PROPERTY(MeshColliderComponent, IsTrigger, "触发器", Bool)

    REGISTER_COMPONENT_BEGIN(MeshColliderComponent)
    REGISTER_COMPONENT_PROPERTY(MeshColliderComponent, Type)
    REGISTER_COMPONENT_PROPERTY(MeshColliderComponent, MeshPath)
    REGISTER_COMPONENT_PROPERTY(MeshColliderComponent, IsTrigger)
    REGISTER_COMPONENT_END(MeshColliderComponent)

    // ---- CollisionParticleTriggerComponent 反射 ----

    ENGINE_COMPONENT(CollisionParticleTriggerComponent, "碰撞粒子触发器")
    ENGINE_PROPERTY(CollisionParticleTriggerComponent, Enabled, "启用", Bool)
    ENGINE_PROPERTY_EX(CollisionParticleTriggerComponent, BurstOnCollision, "爆发粒子数", Int, hints.Speed = 1.0f;
                       hints.Min = 1.0f; hints.Max = 1000.0f)
    ENGINE_PROPERTY_EX(CollisionParticleTriggerComponent, MinImpulse, "最小冲量", Float, hints.Speed = 0.1f;
                       hints.Min = 0.0f; hints.Max = 100.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY(CollisionParticleTriggerComponent, UseCollisionNormal, "使用碰撞法线", Bool)
    ENGINE_PROPERTY_EX(CollisionParticleTriggerComponent, MaxBurstPerFrame, "每帧最大爆发", Int, hints.Speed = 10.0f;
                       hints.Min = 1.0f; hints.Max = 10000.0f)

    REGISTER_COMPONENT_BEGIN(CollisionParticleTriggerComponent)
    REGISTER_COMPONENT_PROPERTY(CollisionParticleTriggerComponent, Enabled)
    REGISTER_COMPONENT_PROPERTY(CollisionParticleTriggerComponent, BurstOnCollision)
    REGISTER_COMPONENT_PROPERTY(CollisionParticleTriggerComponent, MinImpulse)
    REGISTER_COMPONENT_PROPERTY(CollisionParticleTriggerComponent, UseCollisionNormal)
    REGISTER_COMPONENT_PROPERTY(CollisionParticleTriggerComponent, MaxBurstPerFrame)
    REGISTER_COMPONENT_END(CollisionParticleTriggerComponent)

    // ---- RigidBodyComponent 反射 ----

    static const char* s_BodyTypeNames[] = {"静态", "动态", "运动学"};

    ENGINE_COMPONENT(RigidBodyComponent, "刚体")
    ENGINE_PROPERTY_EX(RigidBodyComponent, Type, "类型", Enum, hints.EnumNames = s_BodyTypeNames; hints.EnumCount = 3)
    ENGINE_PROPERTY_EX(RigidBodyComponent, Mass, "质量", Float, hints.Speed = 0.1f; hints.Min = 0.01f;
                       hints.Max = 1000.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(RigidBodyComponent, Restitution, "弹性系数", Float, hints.Speed = 0.01f; hints.Min = 0.0f;
                       hints.Max = 1.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(RigidBodyComponent, Friction, "摩擦系数", Float, hints.Speed = 0.01f; hints.Min = 0.0f;
                       hints.Max = 1.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(RigidBodyComponent, GravityScale, "重力缩放", Float, hints.Speed = 0.1f; hints.Min = -10.0f;
                       hints.Max = 10.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY(RigidBodyComponent, FixedRotation, "固定旋转", Bool)
    ENGINE_PROPERTY_EX(RigidBodyComponent, LinearVelocity, "线速度", Vec3, hints.Transient = true)
    ENGINE_PROPERTY_EX(RigidBodyComponent, AngularVelocity, "角速度", Vec3, hints.Transient = true)
    ENGINE_PROPERTY_EX(RigidBodyComponent, Force, "力", Vec3, hints.Transient = true)
    ENGINE_PROPERTY_EX(RigidBodyComponent, Torque, "力矩", Vec3, hints.Transient = true)

    REGISTER_COMPONENT_BEGIN(RigidBodyComponent)
    REGISTER_COMPONENT_PROPERTY(RigidBodyComponent, Type)
    REGISTER_COMPONENT_PROPERTY(RigidBodyComponent, Mass)
    REGISTER_COMPONENT_PROPERTY(RigidBodyComponent, Restitution)
    REGISTER_COMPONENT_PROPERTY(RigidBodyComponent, Friction)
    REGISTER_COMPONENT_PROPERTY(RigidBodyComponent, GravityScale)
    REGISTER_COMPONENT_PROPERTY(RigidBodyComponent, FixedRotation)
    REGISTER_COMPONENT_PROPERTY(RigidBodyComponent, LinearVelocity)
    REGISTER_COMPONENT_PROPERTY(RigidBodyComponent, AngularVelocity)
    REGISTER_COMPONENT_PROPERTY(RigidBodyComponent, Force)
    REGISTER_COMPONENT_PROPERTY(RigidBodyComponent, Torque)
    REGISTER_COMPONENT_END(RigidBodyComponent)

    // ---- LightComponent 反射 ----

    static const char* s_LightTypeNames[] = {"方向光", "点光源", "聚光灯"};

    ENGINE_COMPONENT(LightComponent, "灯光")
    ENGINE_PROPERTY_EX(LightComponent, Type, "灯光类型", Enum, hints.EnumNames = s_LightTypeNames; hints.EnumCount = 3)
    ENGINE_PROPERTY(LightComponent, Color, "颜色", Color3)
    ENGINE_PROPERTY_EX(LightComponent, Intensity, "强度", Float, hints.Speed = 0.05f; hints.Min = 0.0f;
                       hints.Max = 100.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(LightComponent, Constant, "常数项", Float, hints.Speed = 0.01f; hints.Min = 0.001f;
                       hints.Max = 10.0f; hints.Format = "%.3f"; hints.Group = "衰减")
    ENGINE_PROPERTY_EX(LightComponent, Linear, "线性项", Float, hints.Speed = 0.001f; hints.Min = 0.0f;
                       hints.Max = 1.0f; hints.Format = "%.4f")
    ENGINE_PROPERTY_EX(LightComponent, Quadratic, "二次项", Float, hints.Speed = 0.001f; hints.Min = 0.0f;
                       hints.Max = 1.0f; hints.Format = "%.4f")
    ENGINE_PROPERTY_EX(LightComponent, InnerCutoff, "内锥角", Float, hints.Speed = 0.001f; hints.Group = "锥角")
    ENGINE_PROPERTY_EX(LightComponent, OuterCutoff, "外锥角", Float, hints.Speed = 0.001f)
    ENGINE_PROPERTY(LightComponent, CastShadows, "投射阴影", Bool)

    REGISTER_COMPONENT_BEGIN(LightComponent)
    REGISTER_COMPONENT_PROPERTY(LightComponent, Type)
    REGISTER_COMPONENT_PROPERTY(LightComponent, Color)
    REGISTER_COMPONENT_PROPERTY(LightComponent, Intensity)
    REGISTER_COMPONENT_PROPERTY(LightComponent, Constant)
    REGISTER_COMPONENT_PROPERTY(LightComponent, Linear)
    REGISTER_COMPONENT_PROPERTY(LightComponent, Quadratic)
    REGISTER_COMPONENT_PROPERTY(LightComponent, InnerCutoff)
    REGISTER_COMPONENT_PROPERTY(LightComponent, OuterCutoff)
    REGISTER_COMPONENT_PROPERTY(LightComponent, CastShadows)
    REGISTER_COMPONENT_END(LightComponent)

    // ---- FluidEmitterComponent 反射 ----

    ENGINE_COMPONENT(FluidEmitterComponent, "流体发射器")

    // 发射参数
    ENGINE_PROPERTY_EX(FluidEmitterComponent, ParticleCount, "粒子数量", UInt32, hints.Speed = 100.0f;
                       hints.Min = 100.0f; hints.Max = 100000.0f)
    ENGINE_PROPERTY_EX(FluidEmitterComponent, ParticleRadius, "粒子半径", Float, hints.Speed = 0.001f;
                       hints.Min = 0.001f; hints.Max = 1.0f; hints.Format = "%.3f")
    ENGINE_PROPERTY(FluidEmitterComponent, EmitExtents, "发射区域半尺寸", Vec3)
    ENGINE_PROPERTY(FluidEmitterComponent, InitialVelocity, "初始速度", Vec3)

    // SPH 参数
    ENGINE_PROPERTY_EX(FluidEmitterComponent, RestDensity, "静止密度", Float, hints.Speed = 1.0f; hints.Min = 1.0f;
                       hints.Max = 10000.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, GasConstant, "气体常数", Float, hints.Speed = 0.5f; hints.Min = 0.0f;
                       hints.Max = 500.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, Viscosity, "粘性系数", Float, hints.Speed = 0.1f; hints.Min = 0.0f;
                       hints.Max = 100.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, SmoothingRadius, "光滑核半径", Float, hints.Speed = 0.005f;
                       hints.Min = 0.01f; hints.Max = 1.0f; hints.Format = "%.3f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, ParticleMass, "粒子质量", Float, hints.Speed = 0.001f; hints.Min = 0.001f;
                       hints.Max = 1.0f; hints.Format = "%.3f")
    ENGINE_PROPERTY(FluidEmitterComponent, Gravity, "重力", Vec3)
    ENGINE_PROPERTY_EX(FluidEmitterComponent, Damping, "速度阻尼", Float, hints.Speed = 0.001f; hints.Min = 0.9f;
                       hints.Max = 1.0f; hints.Format = "%.3f")

    // PCISPH
    ENGINE_PROPERTY(FluidEmitterComponent, PCISPHEnabled, "PCISPH 启用", Bool)
    ENGINE_PROPERTY_EX(FluidEmitterComponent, PCISPHIterations, "PCISPH 迭代次数", Int, hints.Speed = 1.0f;
                       hints.Min = 1.0f; hints.Max = 8.0f)
    ENGINE_PROPERTY_EX(FluidEmitterComponent, PCISPHDelta, "PCISPH Delta", Float, hints.Speed = 0.01f;
                       hints.Min = 0.01f; hints.Max = 1.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, SurfaceTension, "表面张力", Float, hints.Speed = 0.01f; hints.Min = 0.0f;
                       hints.Max = 10.0f; hints.Format = "%.2f")

    // 刚体耦合
    ENGINE_PROPERTY(FluidEmitterComponent, RigidBodyCoupling, "刚体耦合", Bool)
    ENGINE_PROPERTY_EX(FluidEmitterComponent, BoundaryStiffness, "边界刚度", Float, hints.Speed = 100.0f;
                       hints.Min = 0.0f; hints.Max = 50000.0f; hints.Format = "%.0f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, BoundaryDamping, "边界阻尼", Float, hints.Speed = 0.01f; hints.Min = 0.0f;
                       hints.Max = 1.0f; hints.Format = "%.2f")

    // 边界盒
    ENGINE_PROPERTY(FluidEmitterComponent, UseBoundary, "使用边界盒", Bool)
    ENGINE_PROPERTY(FluidEmitterComponent, BoundaryMin, "边界最小值", Vec3)
    ENGINE_PROPERTY(FluidEmitterComponent, BoundaryMax, "边界最大值", Vec3)

    // 渲染参数
    ENGINE_PROPERTY(FluidEmitterComponent, FluidColor, "流体颜色", Color3)
    ENGINE_PROPERTY(FluidEmitterComponent, AbsorptionColor, "吸收颜色", Color3)
    ENGINE_PROPERTY_EX(FluidEmitterComponent, AbsorptionScale, "吸收强度", Float, hints.Speed = 0.1f; hints.Min = 0.0f;
                       hints.Max = 20.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, FresnelPower, "Fresnel 指数", Float, hints.Speed = 0.1f; hints.Min = 0.1f;
                       hints.Max = 10.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, RefractionStrength, "折射强度", Float, hints.Speed = 0.01f;
                       hints.Min = 0.0f; hints.Max = 1.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, Reflectivity, "反射率", Float, hints.Speed = 0.01f; hints.Min = 0.0f;
                       hints.Max = 1.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, SmoothIterations, "平滑迭代次数", Int, hints.Speed = 1.0f;
                       hints.Min = 0.0f; hints.Max = 10.0f)
    ENGINE_PROPERTY_EX(FluidEmitterComponent, SmoothFilterRadius, "平滑核半径", Float, hints.Speed = 0.5f;
                       hints.Min = 1.0f; hints.Max = 30.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY_EX(FluidEmitterComponent, SmoothDepthFalloff, "深度衰减", Float, hints.Speed = 1.0f;
                       hints.Min = 1.0f; hints.Max = 500.0f; hints.Format = "%.0f")

    REGISTER_COMPONENT_BEGIN(FluidEmitterComponent)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, ParticleCount)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, ParticleRadius)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, EmitExtents)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, InitialVelocity)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, RestDensity)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, GasConstant)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, Viscosity)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, SmoothingRadius)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, ParticleMass)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, Gravity)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, Damping)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, PCISPHEnabled)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, PCISPHIterations)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, PCISPHDelta)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, SurfaceTension)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, RigidBodyCoupling)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, BoundaryStiffness)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, BoundaryDamping)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, UseBoundary)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, BoundaryMin)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, BoundaryMax)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, FluidColor)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, AbsorptionColor)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, AbsorptionScale)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, FresnelPower)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, RefractionStrength)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, Reflectivity)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, SmoothIterations)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, SmoothFilterRadius)
    REGISTER_COMPONENT_PROPERTY(FluidEmitterComponent, SmoothDepthFalloff)
    REGISTER_COMPONENT_END(FluidEmitterComponent)

    // ---- AudioSourceComponent 反射 ----

    ENGINE_COMPONENT(AudioSourceComponent, "音频源")
    ENGINE_PROPERTY_EX(AudioSourceComponent, AudioPath, "音频路径", AssetPath, hints.FileFilter = "*.wav;*.mp3;*.ogg";
                       hints.FileDesc = "音频文件")
    ENGINE_PROPERTY_EX(AudioSourceComponent, Volume, "音量", Float, hints.Speed = 0.01f; hints.Min = 0.0f;
                       hints.Max = 2.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(AudioSourceComponent, Pitch, "音调", Float, hints.Speed = 0.01f; hints.Min = 0.1f;
                       hints.Max = 3.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(AudioSourceComponent, MinDistance, "最小距离", Float, hints.Speed = 0.1f; hints.Min = 0.0f;
                       hints.Max = 100.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY_EX(AudioSourceComponent, MaxDistance, "最大距离", Float, hints.Speed = 1.0f; hints.Min = 0.0f;
                       hints.Max = 1000.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY(AudioSourceComponent, Loop, "循环", Bool)
    ENGINE_PROPERTY(AudioSourceComponent, PlayOnStart, "启动播放", Bool)
    ENGINE_PROPERTY(AudioSourceComponent, Spatial, "3D空间音效", Bool)

    REGISTER_COMPONENT_BEGIN(AudioSourceComponent)
    REGISTER_COMPONENT_FLAGS(ComponentMeta::CustomUI)
    REGISTER_COMPONENT_PROPERTY(AudioSourceComponent, AudioPath)
    REGISTER_COMPONENT_PROPERTY(AudioSourceComponent, Volume)
    REGISTER_COMPONENT_PROPERTY(AudioSourceComponent, Pitch)
    REGISTER_COMPONENT_PROPERTY(AudioSourceComponent, MinDistance)
    REGISTER_COMPONENT_PROPERTY(AudioSourceComponent, MaxDistance)
    REGISTER_COMPONENT_PROPERTY(AudioSourceComponent, Loop)
    REGISTER_COMPONENT_PROPERTY(AudioSourceComponent, PlayOnStart)
    REGISTER_COMPONENT_PROPERTY(AudioSourceComponent, Spatial)
    REGISTER_COMPONENT_END(AudioSourceComponent)

    // ---- AudioListenerComponent 反射 ----

    ENGINE_COMPONENT(AudioListenerComponent, "音频监听器")
    ENGINE_PROPERTY(AudioListenerComponent, Active, "激活", Bool)

    REGISTER_COMPONENT_BEGIN(AudioListenerComponent)
    REGISTER_COMPONENT_FLAGS(ComponentMeta::CustomUI)
    REGISTER_COMPONENT_PROPERTY(AudioListenerComponent, Active)
    REGISTER_COMPONENT_END(AudioListenerComponent)

    // ---- VideoPlayerComponent 反射 ----

    ENGINE_COMPONENT(VideoPlayerComponent, "视频播放器")
    ENGINE_PROPERTY(VideoPlayerComponent, StreamURL, "流地址", String)
    ENGINE_PROPERTY(VideoPlayerComponent, PlayOnStart, "启动播放", Bool)
    ENGINE_PROPERTY(VideoPlayerComponent, Loop, "循环", Bool)
    ENGINE_PROPERTY_EX(VideoPlayerComponent, Volume, "音量", Float, hints.Speed = 0.01f; hints.Min = 0.0f;
                       hints.Max = 2.0f; hints.Format = "%.2f")

    REGISTER_COMPONENT_BEGIN(VideoPlayerComponent)
    REGISTER_COMPONENT_FLAGS(ComponentMeta::CustomUI)
    REGISTER_COMPONENT_PROPERTY(VideoPlayerComponent, StreamURL)
    REGISTER_COMPONENT_PROPERTY(VideoPlayerComponent, PlayOnStart)
    REGISTER_COMPONENT_PROPERTY(VideoPlayerComponent, Loop)
    REGISTER_COMPONENT_PROPERTY(VideoPlayerComponent, Volume)
    REGISTER_COMPONENT_END(VideoPlayerComponent)

    // ---- ParticleEmitterComponent 反射（仅注册，CustomUI | CustomSerial）----

    ENGINE_COMPONENT(ParticleEmitterComponent, "粒子发射器")

    REGISTER_COMPONENT_BEGIN(ParticleEmitterComponent)
    REGISTER_COMPONENT_FLAGS(ComponentMeta::CustomUI | ComponentMeta::CustomSerial)
    REGISTER_COMPONENT_END(ParticleEmitterComponent)

    // ---- CameraComponent 反射（仅注册，CustomUI | CustomSerial）----

    ENGINE_COMPONENT(CameraComponent, "相机")

    REGISTER_COMPONENT_BEGIN(CameraComponent)
    REGISTER_COMPONENT_FLAGS(ComponentMeta::CustomUI | ComponentMeta::CustomSerial)
    REGISTER_COMPONENT_END(CameraComponent)

    // ---- MeshRendererComponent 反射（仅注册，CustomUI | CustomSerial）----

    ENGINE_COMPONENT(MeshRendererComponent, "网格渲染器")

    REGISTER_COMPONENT_BEGIN(MeshRendererComponent)
    REGISTER_COMPONENT_FLAGS(ComponentMeta::CustomUI | ComponentMeta::CustomSerial)
    REGISTER_COMPONENT_END(MeshRendererComponent)

    // ---- TerrainComponent 反射（仅注册，CustomUI | CustomSerial）----

    ENGINE_COMPONENT(TerrainComponent, "地形")

    REGISTER_COMPONENT_BEGIN(TerrainComponent)
    REGISTER_COMPONENT_FLAGS(ComponentMeta::CustomUI | ComponentMeta::CustomSerial)
    REGISTER_COMPONENT_END(TerrainComponent)

    // ---- NativeScriptComponent 反射（仅注册，CustomUI | CustomSerial）----

    ENGINE_COMPONENT(NativeScriptComponent, "脚本")

    REGISTER_COMPONENT_BEGIN(NativeScriptComponent)
    REGISTER_COMPONENT_FLAGS(ComponentMeta::CustomUI | ComponentMeta::CustomSerial)
    REGISTER_COMPONENT_END(NativeScriptComponent)

    // 强制链接：引用所有注册变量，防止链接器丢弃
    void ComponentRegistry::EnsureRegistered()
    {
        (void)s_Registered_BoxColliderComponent;
        (void)s_Registered_SphereColliderComponent;
        (void)s_Registered_MeshColliderComponent;
        (void)s_Registered_CollisionParticleTriggerComponent;
        (void)s_Registered_RigidBodyComponent;
        (void)s_Registered_LightComponent;
        (void)s_Registered_FluidEmitterComponent;
        (void)s_Registered_AudioSourceComponent;
        (void)s_Registered_AudioListenerComponent;
        (void)s_Registered_VideoPlayerComponent;
        (void)s_Registered_ParticleEmitterComponent;
        (void)s_Registered_CameraComponent;
        (void)s_Registered_MeshRendererComponent;
        (void)s_Registered_TerrainComponent;
        (void)s_Registered_NativeScriptComponent;
    }

} // namespace Engine
