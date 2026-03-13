#include "engpch.h"
#include "Scene/SceneHierarchyService.h"
#include "Scene/Components.h"
#include "Scene/SceneEntityIndex.h"
#include "Scene/WorldTransformService.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Engine
{

    void SceneHierarchyService::SetParent(entt::registry& reg, const SceneEntityIndex& index,
                                          entt::entity child, entt::entity parent)
    {
        if (child == entt::null || parent == entt::null)
            return;
        if (child == parent)
            return;

        // 防止循环：parent 不能是 child 的后代
        if (IsAncestorOf(reg, index, child, parent))
            return;

        // 1. 记录子实体当前世界变换
        glm::mat4 childWorldMatrix = WorldTransformService::ComputeWorldTransform(reg, child, index);

        // 2. 先解除旧的父子关系（不调用 RemoveParent 以避免变换转换）
        if (reg.all_of<RelationshipComponent>(child))
        {
            auto& oldChildRel = reg.get<RelationshipComponent>(child);
            if (static_cast<uint64_t>(oldChildRel.ParentID) != 0)
            {
                entt::entity oldParent = index.Find(oldChildRel.ParentID);
                if (oldParent != entt::null && reg.valid(oldParent) && reg.all_of<RelationshipComponent>(oldParent))
                {
                    auto& oldParentChildren = reg.get<RelationshipComponent>(oldParent).Children;
                    UUID childUUID = reg.get<IDComponent>(child).ID;
                    oldParentChildren.erase(
                        std::remove_if(oldParentChildren.begin(), oldParentChildren.end(), [childUUID](UUID id)
                                       { return static_cast<uint64_t>(id) == static_cast<uint64_t>(childUUID); }),
                        oldParentChildren.end());
                }
                oldChildRel.ParentID = 0;
            }
        }

        // 3. 建立新的父子关系
        auto& childRel = reg.get<RelationshipComponent>(child);
        auto& parentRel = reg.get<RelationshipComponent>(parent);

        childRel.ParentID = reg.get<IDComponent>(parent).ID;
        parentRel.Children.push_back(reg.get<IDComponent>(child).ID);

        // 4. 计算新父物体的世界变换，将子物体世界变换转为相对于新父的本地变换
        glm::mat4 parentWorldMatrix = WorldTransformService::ComputeWorldTransform(reg, parent, index);
        glm::mat4 childLocalMatrix = glm::inverse(parentWorldMatrix) * childWorldMatrix;

        // 5. 从本地矩阵分解出 Translation / Rotation / Scale 写回子实体
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(childLocalMatrix, scale, rotation, translation, skew, perspective);

        auto& childTransform = reg.get<TransformComponent>(child);
        childTransform.Translation = translation;
        childTransform.Rotation = glm::eulerAngles(rotation);
        childTransform.Scale = scale;
    }

    void SceneHierarchyService::RemoveParent(entt::registry& reg, const SceneEntityIndex& index,
                                             entt::entity child)
    {
        if (child == entt::null || !reg.all_of<RelationshipComponent>(child))
            return;

        auto& childRel = reg.get<RelationshipComponent>(child);
        if (static_cast<uint64_t>(childRel.ParentID) == 0)
            return;

        // 1. 记录子实体当前世界变换（解除前还有父物体）
        glm::mat4 childWorldMatrix = WorldTransformService::ComputeWorldTransform(reg, child, index);

        // 2. 从父实体的 Children 列表中移除自身
        entt::entity parent = index.Find(childRel.ParentID);
        if (parent != entt::null && reg.valid(parent) && reg.all_of<RelationshipComponent>(parent))
        {
            auto& parentChildren = reg.get<RelationshipComponent>(parent).Children;
            UUID childUUID = reg.get<IDComponent>(child).ID;
            parentChildren.erase(
                std::remove_if(parentChildren.begin(), parentChildren.end(), [childUUID](UUID id)
                               { return static_cast<uint64_t>(id) == static_cast<uint64_t>(childUUID); }),
                parentChildren.end());
        }

        childRel.ParentID = 0;

        // 3. 解除后成为根节点，世界变换 = 本地变换，需把之前的世界变换写回
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(childWorldMatrix, scale, rotation, translation, skew, perspective);

        auto& childTransform = reg.get<TransformComponent>(child);
        childTransform.Translation = translation;
        childTransform.Rotation = glm::eulerAngles(rotation);
        childTransform.Scale = scale;
    }

    std::vector<entt::entity> SceneHierarchyService::GetChildren(entt::registry& reg, const SceneEntityIndex& index,
                                                                  entt::entity parent)
    {
        std::vector<entt::entity> result;
        if (parent == entt::null || !reg.all_of<RelationshipComponent>(parent))
            return result;

        auto& rel = reg.get<RelationshipComponent>(parent);
        for (auto childUUID : rel.Children)
        {
            entt::entity child = index.Find(childUUID);
            if (child != entt::null && reg.valid(child))
                result.push_back(child);
        }
        return result;
    }

    bool SceneHierarchyService::IsAncestorOf(entt::registry& reg, const SceneEntityIndex& index,
                                             entt::entity ancestor, entt::entity entity)
    {
        if (entity == entt::null || !reg.all_of<RelationshipComponent>(entity))
            return false;

        auto& rel = reg.get<RelationshipComponent>(entity);
        if (static_cast<uint64_t>(rel.ParentID) == 0)
            return false;

        // 比较 ancestor 的 UUID
        UUID ancestorUUID = reg.get<IDComponent>(ancestor).ID;
        if (rel.ParentID == ancestorUUID)
            return true;

        entt::entity parent = index.Find(rel.ParentID);
        return (parent != entt::null && reg.valid(parent)) ? IsAncestorOf(reg, index, ancestor, parent) : false;
    }

    std::vector<entt::entity> SceneHierarchyService::GetRootEntities(entt::registry& reg)
    {
        std::vector<entt::entity> roots;
        auto view = reg.view<IDComponent, RelationshipComponent>();
        for (auto entity : view)
        {
            auto& rel = view.get<RelationshipComponent>(entity);
            if (static_cast<uint64_t>(rel.ParentID) == 0)
                roots.push_back(entity);
        }
        return roots;
    }

} // namespace Engine
