#pragma once

#include "Reflection/PropertyInfo.h"

#include <any>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Engine
{
    class Scene;

    struct ComponentMeta
    {
        const char* TypeName;
        const char* DisplayName;
        std::vector<PropertyInfo> Properties;

        // 类型擦除的组件操作
        std::function<bool(Scene&, uint32_t)> Has;
        std::function<void(Scene&, uint32_t)> Add;
        std::function<void*(Scene&, uint32_t)> Get;
        std::function<void(Scene&, uint32_t)> Remove;
        std::function<void(Scene&, uint32_t, Scene&, uint32_t)> Copy;

        // 策略标志（替代 ComponentPolicies.h）
        enum Flag : uint32_t
        {
            None         = 0,
            CustomUI     = 1 << 0, // 保留手写 Inspector（跳过自动绘制）
            CustomSerial = 1 << 1, // 保留手写序列化（跳过 AutoSerializer）
        };
        uint32_t Flags = None;

        // 类型擦除的快照/恢复（用于 Undo 系统）
        std::function<std::any(Scene&, uint32_t)> Snapshot;
        std::function<void(Scene&, uint32_t, const std::any&)> Restore;
    };

} // namespace Engine

// ---- 反射宏 ----

// 声明组件元数据（在 struct 外部使用）
#define ENGINE_COMPONENT(CompType, displayName)                                                                        \
    namespace _Reflect_##CompType                                                                                      \
    {                                                                                                                  \
        inline ::Engine::ComponentMeta& GetMeta()                                                                      \
        {                                                                                                              \
            static ::Engine::ComponentMeta s_Meta;                                                                     \
            s_Meta.TypeName = #CompType;                                                                               \
            s_Meta.DisplayName = displayName;                                                                          \
            s_Meta.Properties.clear();                                                                                 \
            return s_Meta;                                                                                             \
        }                                                                                                              \
    }

// 标记属性（基础版）
#define ENGINE_PROPERTY(CompType, field, displayName, propType)                                                        \
    namespace _Reflect_##CompType                                                                                      \
    {                                                                                                                  \
        namespace _Prop_##field                                                                                        \
        {                                                                                                              \
            inline void Register(::Engine::ComponentMeta& meta)                                                        \
            {                                                                                                          \
                ::Engine::PropertyInfo info;                                                                           \
                info.Name = #field;                                                                                    \
                info.DisplayName = displayName;                                                                        \
                info.Type = ::Engine::PropertyType::propType;                                                          \
                info.Offset = offsetof(::Engine::CompType, field);                                                     \
                info.Hints = {};                                                                                       \
                meta.Properties.push_back(info);                                                                       \
            }                                                                                                          \
        }                                                                                                              \
    }

// 标记属性（带 hints 扩展版）
#define ENGINE_PROPERTY_EX(CompType, field, displayName, propType, hintsCode)                                          \
    namespace _Reflect_##CompType                                                                                      \
    {                                                                                                                  \
        namespace _Prop_##field                                                                                        \
        {                                                                                                              \
            inline void Register(::Engine::ComponentMeta& meta)                                                        \
            {                                                                                                          \
                ::Engine::PropertyInfo info;                                                                           \
                info.Name = #field;                                                                                    \
                info.DisplayName = displayName;                                                                        \
                info.Type = ::Engine::PropertyType::propType;                                                          \
                info.Offset = offsetof(::Engine::CompType, field);                                                     \
                ::Engine::PropertyHints hints{};                                                                       \
                hintsCode;                                                                                             \
                info.Hints = hints;                                                                                    \
                meta.Properties.push_back(info);                                                                       \
            }                                                                                                          \
        }                                                                                                              \
    }

// 设置策略标志（在 REGISTER_COMPONENT_BEGIN/END 块内使用）
#define REGISTER_COMPONENT_FLAGS(flags) meta.Flags = (flags);

// 在 .cpp 中触发注册（组合所有属性 + 注册到全局表）
// 使用 __VA_ARGS__ 展开所有字段
#define REGISTER_COMPONENT_BEGIN(CompType) static bool s_Registered_##CompType = []() -> bool { \
        auto& meta = _Reflect_##CompType::GetMeta(); \
        meta.Has = [](::Engine::Scene& scene, uint32_t entity) -> bool { \
            return scene.GetRegistry().all_of<::Engine::CompType>(static_cast<entt::entity>(entity)); \
        }; \
        meta.Add = [](::Engine::Scene& scene, uint32_t entity) { \
            scene.GetRegistry().emplace<::Engine::CompType>(static_cast<entt::entity>(entity)); \
        }; \
        meta.Get = [](::Engine::Scene& scene, uint32_t entity) -> void* { \
            return &scene.GetRegistry().get<::Engine::CompType>(static_cast<entt::entity>(entity)); \
        }; \
        meta.Remove = [](::Engine::Scene& scene, uint32_t entity) { \
            scene.GetRegistry().remove<::Engine::CompType>(static_cast<entt::entity>(entity)); \
        }; \
        meta.Copy = [](::Engine::Scene& src, uint32_t srcEntity, ::Engine::Scene& dst, uint32_t dstEntity) { \
            if (src.GetRegistry().all_of<::Engine::CompType>(static_cast<entt::entity>(srcEntity))) { \
                auto comp = src.GetRegistry().get<::Engine::CompType>(static_cast<entt::entity>(srcEntity)); \
                dst.GetRegistry().emplace_or_replace<::Engine::CompType>(static_cast<entt::entity>(dstEntity), comp); \
            } \
        }; \
        meta.Snapshot = [](::Engine::Scene& scene, uint32_t entity) -> std::any { \
            return scene.GetRegistry().get<::Engine::CompType>(static_cast<entt::entity>(entity)); \
        }; \
        meta.Restore = [](::Engine::Scene& scene, uint32_t entity, const std::any& data) { \
            scene.GetRegistry().emplace_or_replace<::Engine::CompType>( \
                static_cast<entt::entity>(entity), std::any_cast<const ::Engine::CompType&>(data)); \
        };

#define REGISTER_COMPONENT_PROPERTY(CompType, field) _Reflect_##CompType::_Prop_##field::Register(meta);

#define REGISTER_COMPONENT_END(CompType)                                                                               \
    ::Engine::ComponentRegistry::Instance().Register(meta);                                                            \
    return true;                                                                                                       \
    }                                                                                                                  \
    ();

// 便捷宏：无属性组件直接注册
#define REGISTER_COMPONENT(CompType)                                                                                   \
    REGISTER_COMPONENT_BEGIN(CompType)                                                                                 \
    REGISTER_COMPONENT_END(CompType)
