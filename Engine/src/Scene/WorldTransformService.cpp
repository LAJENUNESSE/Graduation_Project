#include "engpch.h"
#include "Scene/WorldTransformService.h"
#include "Scene/Components.h"
#include "Scene/SceneEntityIndex.h"

namespace Engine
{

    glm::mat4 WorldTransformService::ComputeWorldTransform(entt::registry& reg, entt::entity entity,
                                                           const SceneEntityIndex& index)
    {
        auto& transform = reg.get<TransformComponent>(entity);
        glm::mat4 localMatrix = transform.GetTransform();

        if (reg.all_of<RelationshipComponent>(entity))
        {
            auto& rel = reg.get<RelationshipComponent>(entity);
            if (static_cast<uint64_t>(rel.ParentID) != 0)
            {
                entt::entity parent = index.Find(rel.ParentID);
                if (parent != entt::null && reg.valid(parent))
                    return ComputeWorldTransform(reg, parent, index) * localMatrix;
            }
        }

        return localMatrix;
    }

} // namespace Engine
