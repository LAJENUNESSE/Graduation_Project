#pragma once

#include "Core/Timestep.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"
#include "Core/Input.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"

namespace Engine
{

    class ScriptableEntity
    {
    public:
        virtual ~ScriptableEntity() = default;

        virtual void OnCreate() {}
        virtual void OnUpdate(Timestep ts) {}
        virtual void OnDestroy() {}

    protected:
        template <typename T>
        T& GetComponent()
        {
            return m_Entity.GetComponent<T>();
        }

        template <typename T>
        bool HasComponent()
        {
            return m_Entity.HasComponent<T>();
        }

        TransformComponent& GetTransform()
        {
            return m_Entity.GetComponent<TransformComponent>();
        }

        bool IsKeyPressed(KeyCode key)
        {
            return Input::IsKeyPressed(key);
        }

        bool IsMouseButtonPressed(MouseCode button)
        {
            return Input::IsMouseButtonPressed(button);
        }

    private:
        Entity m_Entity;
        friend class Scene;
    };

} // namespace Engine
