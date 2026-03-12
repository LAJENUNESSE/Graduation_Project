#include "Scene/Services/TransformHierarchyService.h"
#include "Scene/Components.h"

namespace Engine
{

    glm::mat4 TransformHierarchyService::ComputeWorldTransform(entt::registry& reg, entt::entity entity)
    {
        if (!reg.valid(entity) || !reg.all_of<TransformComponent>(entity))
            return glm::mat4(1.0f);

        auto& transform = reg.get<TransformComponent>(entity);
        glm::mat4 localMatrix = transform.GetTransform();

        if (reg.all_of<RelationshipComponent>(entity))
        {
            auto& rel = reg.get<RelationshipComponent>(entity);
            if (static_cast<uint64_t>(rel.ParentID) != 0)
            {
                auto view = reg.view<IDComponent>();
                for (auto e : view)
                {
                    if (view.get<IDComponent>(e).ID == rel.ParentID)
                        return ComputeWorldTransform(reg, e) * localMatrix;
                }
            }
        }

        return localMatrix;
    }

} // namespace Engine
