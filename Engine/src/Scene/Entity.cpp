#include "engpch.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"

namespace Engine
{

    Entity::Entity(entt::entity handle, Scene* scene) : m_EntityHandle(handle), m_Scene(scene) {}

    UUID Entity::GetUUID()
    {
        return GetComponent<IDComponent>().ID;
    }

    const std::string& Entity::GetName()
    {
        return GetComponent<TagComponent>().Tag;
    }

} // namespace Engine
