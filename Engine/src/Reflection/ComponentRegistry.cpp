#include "engpch.h"
#include "Reflection/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"

#include <entt/entt.hpp>

namespace Engine
{

    // ---- BoxColliderComponent 反射 ----

    ENGINE_COMPONENT(BoxColliderComponent, "盒碰撞器")
    ENGINE_PROPERTY(BoxColliderComponent, HalfExtents, "半尺寸", Vec3)
    ENGINE_PROPERTY(BoxColliderComponent, Offset, "偏移", Vec3)

    REGISTER_COMPONENT_BEGIN(BoxColliderComponent)
    REGISTER_COMPONENT_PROPERTY(BoxColliderComponent, HalfExtents)
    REGISTER_COMPONENT_PROPERTY(BoxColliderComponent, Offset)
    REGISTER_COMPONENT_END(BoxColliderComponent)

    // ---- SphereColliderComponent 反射 ----

    ENGINE_COMPONENT(SphereColliderComponent, "球碰撞器")
    ENGINE_PROPERTY_EX(SphereColliderComponent, Radius, "半径", Float,
        hints.Speed = 0.01f; hints.Min = 0.01f; hints.Max = 100.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY(SphereColliderComponent, Offset, "偏移", Vec3)

    REGISTER_COMPONENT_BEGIN(SphereColliderComponent)
    REGISTER_COMPONENT_PROPERTY(SphereColliderComponent, Radius)
    REGISTER_COMPONENT_PROPERTY(SphereColliderComponent, Offset)
    REGISTER_COMPONENT_END(SphereColliderComponent)

    // ---- CollisionParticleTriggerComponent 反射 ----

    ENGINE_COMPONENT(CollisionParticleTriggerComponent, "碰撞粒子触发器")
    ENGINE_PROPERTY(CollisionParticleTriggerComponent, Enabled, "启用", Bool)
    ENGINE_PROPERTY_EX(CollisionParticleTriggerComponent, BurstOnCollision, "爆发粒子数", Int,
        hints.Speed = 1.0f; hints.Min = 1.0f; hints.Max = 1000.0f)
    ENGINE_PROPERTY_EX(CollisionParticleTriggerComponent, MinImpulse, "最小冲量", Float,
        hints.Speed = 0.1f; hints.Min = 0.0f; hints.Max = 100.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY(CollisionParticleTriggerComponent, UseCollisionNormal, "使用碰撞法线", Bool)

    REGISTER_COMPONENT_BEGIN(CollisionParticleTriggerComponent)
    REGISTER_COMPONENT_PROPERTY(CollisionParticleTriggerComponent, Enabled)
    REGISTER_COMPONENT_PROPERTY(CollisionParticleTriggerComponent, BurstOnCollision)
    REGISTER_COMPONENT_PROPERTY(CollisionParticleTriggerComponent, MinImpulse)
    REGISTER_COMPONENT_PROPERTY(CollisionParticleTriggerComponent, UseCollisionNormal)
    REGISTER_COMPONENT_END(CollisionParticleTriggerComponent)

    // ---- RigidBodyComponent 反射 ----

    static const char* s_BodyTypeNames[] = {"静态", "动态", "运动学"};

    ENGINE_COMPONENT(RigidBodyComponent, "刚体")
    ENGINE_PROPERTY_EX(RigidBodyComponent, Type, "类型", Enum,
        hints.EnumNames = s_BodyTypeNames; hints.EnumCount = 3)
    ENGINE_PROPERTY_EX(RigidBodyComponent, Mass, "质量", Float,
        hints.Speed = 0.1f; hints.Min = 0.01f; hints.Max = 1000.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(RigidBodyComponent, Restitution, "弹性系数", Float,
        hints.Speed = 0.01f; hints.Min = 0.0f; hints.Max = 1.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(RigidBodyComponent, Friction, "摩擦系数", Float,
        hints.Speed = 0.01f; hints.Min = 0.0f; hints.Max = 1.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(RigidBodyComponent, GravityScale, "重力缩放", Float,
        hints.Speed = 0.1f; hints.Min = -10.0f; hints.Max = 10.0f; hints.Format = "%.1f")
    ENGINE_PROPERTY(RigidBodyComponent, FixedRotation, "固定旋转", Bool)
    ENGINE_PROPERTY_EX(RigidBodyComponent, LinearVelocity, "线速度", Vec3,
        hints.Transient = true)
    ENGINE_PROPERTY_EX(RigidBodyComponent, AngularVelocity, "角速度", Vec3,
        hints.Transient = true)
    ENGINE_PROPERTY_EX(RigidBodyComponent, Force, "力", Vec3,
        hints.Transient = true)
    ENGINE_PROPERTY_EX(RigidBodyComponent, Torque, "力矩", Vec3,
        hints.Transient = true)

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
    ENGINE_PROPERTY_EX(LightComponent, Type, "灯光类型", Enum,
        hints.EnumNames = s_LightTypeNames; hints.EnumCount = 3)
    ENGINE_PROPERTY(LightComponent, Color, "颜色", Color3)
    ENGINE_PROPERTY_EX(LightComponent, Intensity, "强度", Float,
        hints.Speed = 0.05f; hints.Min = 0.0f; hints.Max = 100.0f; hints.Format = "%.2f")
    ENGINE_PROPERTY_EX(LightComponent, Constant, "常数项", Float,
        hints.Speed = 0.01f; hints.Min = 0.001f; hints.Max = 10.0f; hints.Format = "%.3f"; hints.Group = "衰减")
    ENGINE_PROPERTY_EX(LightComponent, Linear, "线性项", Float,
        hints.Speed = 0.001f; hints.Min = 0.0f; hints.Max = 1.0f; hints.Format = "%.4f")
    ENGINE_PROPERTY_EX(LightComponent, Quadratic, "二次项", Float,
        hints.Speed = 0.001f; hints.Min = 0.0f; hints.Max = 1.0f; hints.Format = "%.4f")
    ENGINE_PROPERTY_EX(LightComponent, InnerCutoff, "内锥角", Float,
        hints.Speed = 0.001f; hints.Group = "锥角")
    ENGINE_PROPERTY_EX(LightComponent, OuterCutoff, "外锥角", Float,
        hints.Speed = 0.001f)
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

    // 强制链接：引用所有注册变量，防止链接器丢弃
    void ComponentRegistry::EnsureRegistered()
    {
        (void)s_Registered_BoxColliderComponent;
        (void)s_Registered_SphereColliderComponent;
        (void)s_Registered_CollisionParticleTriggerComponent;
        (void)s_Registered_RigidBodyComponent;
        (void)s_Registered_LightComponent;
    }

} // namespace Engine
