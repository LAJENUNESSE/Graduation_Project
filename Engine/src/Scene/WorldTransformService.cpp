#include "engpch.h"
#include "Scene/WorldTransformService.h"
#include "Core/Log.h"
#include "Scene/Components.h"
#include "Scene/SceneEntityIndex.h"
#include "Scene/WorldTransformCache.h"

namespace Engine
{

    glm::mat4 WorldTransformService::ComputeWorldTransform(entt::registry&         reg,
                                                           entt::entity            entity,
                                                           const SceneEntityIndex& index,
                                                           WorldTransformCache*    cache)
    {
        return ComputeWorldTransformImpl(reg, entity, index, cache, 0);
    }

    glm::mat4 WorldTransformService::ComputeWorldTransformImpl(
        entt::registry& reg, entt::entity entity, const SceneEntityIndex& index, WorldTransformCache* cache, int depth)
    {
        if (depth >= kMaxDepth)
        {
            ENGINE_CORE_WARN("WorldTransformService: max depth ({0}) exceeded, possible cycle in hierarchy", kMaxDepth);
            return reg.get<TransformComponent>(entity).GetTransform();
        }

        if (cache)
        {
            glm::mat4 cached;
            if (cache->TryGet(entity, cached))
                return cached;
        }

        auto&     transform = reg.get<TransformComponent>(entity);
        glm::mat4 result    = transform.GetTransform();

        if (reg.all_of<RelationshipComponent>(entity))
        {
            auto& rel = reg.get<RelationshipComponent>(entity);
            if (static_cast<uint64_t>(rel.ParentID) != 0)
            {
                entt::entity parent = index.Find(rel.ParentID);
                if (parent != entt::null && reg.valid(parent))
                    result = ComputeWorldTransformImpl(reg, parent, index, cache, depth + 1) * result;
            }
        }

        if (cache)
            cache->Put(entity, result);

        return result;
    }

} // namespace Engine
