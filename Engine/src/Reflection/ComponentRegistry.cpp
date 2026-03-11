#include "Reflection/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "engpch.h"

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
    }

} // namespace Engine
