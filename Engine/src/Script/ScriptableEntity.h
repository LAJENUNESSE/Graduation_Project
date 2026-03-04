#pragma once

#include "Core/Timestep.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"
#include "Core/Input.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"

#include <glm/glm.hpp>

namespace Engine
{

    // 碰撞回调信息
    struct CollisionCallbackInfo
    {
        Entity OtherEntity;
        glm::vec3 ContactPoint = {0, 0, 0};
        glm::vec3 ContactNormal = {0, 0, 0};
        float Impulse = 0.0f;
    };

    class ScriptableEntity
    {
    public:
        virtual ~ScriptableEntity() = default;

        virtual void OnCreate() {}
        virtual void OnUpdate(Timestep ts) {}
        virtual void OnDestroy() {}

        // 碰撞回调（需要碰撞体组件）
        virtual void OnCollisionEnter(const CollisionCallbackInfo& info) {}
        virtual void OnCollisionStay(const CollisionCallbackInfo& info) {}
        virtual void OnCollisionExit(Entity other) {}

        // 触发器回调（碰撞体 IsTrigger=true）
        virtual void OnTriggerEnter(Entity other) {}
        virtual void OnTriggerExit(Entity other) {}

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

        Entity GetEntity() const { return m_Entity; }

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
