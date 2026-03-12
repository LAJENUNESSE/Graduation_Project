#include "engpch.h"
#include "Scene/SceneEntityIndex.h"

namespace Engine
{

    void SceneEntityIndex::Insert(UUID uuid, entt::entity entity)
    {
        m_Map[uuid] = entity;
    }

    void SceneEntityIndex::Remove(UUID uuid)
    {
        m_Map.erase(uuid);
    }

    entt::entity SceneEntityIndex::Find(UUID uuid) const
    {
        auto it = m_Map.find(uuid);
        if (it != m_Map.end())
            return it->second;
        return entt::null;
    }

    void SceneEntityIndex::Clear()
    {
        m_Map.clear();
    }

} // namespace Engine
