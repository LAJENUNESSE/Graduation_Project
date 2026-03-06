#pragma once

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Scene/Scene.h"

#include <entt/entt.hpp>

#include <typeinfo>

namespace Engine
{

    class Entity
    {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene);
        Entity(const Entity& other) = default;

        template <typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            ENGINE_CORE_ASSERT(!HasComponent<T>(), "Entity already has component!");
            T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
            if (Log::GetCoreLogger())
                ENGINE_CORE_TRACE("[ComponentLifecycle] Add '{0}' -> entity {1}", typeid(T).name(), static_cast<uint32_t>(m_EntityHandle));
            return component;
        }

        template <typename T, typename... Args>
        T& AddOrReplaceComponent(Args&&... args)
        {
            T& component = m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
            if (Log::GetCoreLogger())
                ENGINE_CORE_TRACE("[ComponentLifecycle] AddOrReplace '{0}' -> entity {1}", typeid(T).name(), static_cast<uint32_t>(m_EntityHandle));
            return component;
        }

        template <typename T>
        T& GetComponent()
        {
            ENGINE_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
            return m_Scene->m_Registry.get<T>(m_EntityHandle);
        }

        template <typename T>
        bool HasComponent()
        {
            return m_Scene->m_Registry.all_of<T>(m_EntityHandle);
        }

        template <typename T>
        void RemoveComponent()
        {
            ENGINE_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
            m_Scene->m_Registry.remove<T>(m_EntityHandle);
            if (Log::GetCoreLogger())
                ENGINE_CORE_TRACE("[ComponentLifecycle] Remove '{0}' <- entity {1}", typeid(T).name(), static_cast<uint32_t>(m_EntityHandle));
        }

        operator bool() const
        {
            return m_EntityHandle != entt::null;
        }

        operator entt::entity() const
        {
            return m_EntityHandle;
        }

        operator uint32_t() const
        {
            return static_cast<uint32_t>(m_EntityHandle);
        }

        bool operator==(const Entity& other) const
        {
            return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
        }

        bool operator!=(const Entity& other) const
        {
            return !(*this == other);
        }

        UUID GetUUID();
        const std::string& GetName();

        Scene* GetScene() const { return m_Scene; }

    private:
        entt::entity m_EntityHandle{entt::null};
        Scene* m_Scene = nullptr;
    };

} // namespace Engine

