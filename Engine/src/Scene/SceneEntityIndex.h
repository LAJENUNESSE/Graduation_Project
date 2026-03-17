#pragma once

#include "Core/UUID.h"

#include <entt/entt.hpp>
#include <unordered_map>

namespace Engine
{

    // O(1) UUID -> entt::entity 查找索引
    // 由 Scene 在 CreateEntity/DestroyEntity 时维护
    class SceneEntityIndex
    {
    public:
        void         Insert(UUID uuid, entt::entity entity);
        void         Remove(UUID uuid);
        entt::entity Find(UUID uuid) const;
        void         Clear();
        size_t       Size() const { return m_Map.size(); }

    private:
        std::unordered_map<UUID, entt::entity> m_Map;
    };

} // namespace Engine
