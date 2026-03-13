#pragma once

#include <entt/entt.hpp>
#include <vector>

namespace Engine
{

    class SceneEntityIndex;

    class SceneHierarchyService
    {
    public:
        static void SetParent(entt::registry& reg, const SceneEntityIndex& index,
                              entt::entity child, entt::entity parent);
        static void RemoveParent(entt::registry& reg, const SceneEntityIndex& index,
                                 entt::entity child);
        static std::vector<entt::entity> GetChildren(entt::registry& reg, const SceneEntityIndex& index,
                                                      entt::entity parent);
        static bool IsAncestorOf(entt::registry& reg, const SceneEntityIndex& index,
                                 entt::entity ancestor, entt::entity entity);
        static std::vector<entt::entity> GetRootEntities(entt::registry& reg);
    };

} // namespace Engine
