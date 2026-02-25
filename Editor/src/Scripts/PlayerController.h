#pragma once

#include "Script/ScriptableEntity.h"

namespace Engine
{

    class PlayerController : public ScriptableEntity
    {
    public:
        float MoveSpeed = 5.0f;

        void OnCreate() override {}

        void OnUpdate(Timestep ts) override
        {
            auto& transform = GetTransform();

            if (IsKeyPressed(KeyCode::W))
                transform.Translation.z -= MoveSpeed * ts;
            if (IsKeyPressed(KeyCode::S))
                transform.Translation.z += MoveSpeed * ts;
            if (IsKeyPressed(KeyCode::A))
                transform.Translation.x -= MoveSpeed * ts;
            if (IsKeyPressed(KeyCode::D))
                transform.Translation.x += MoveSpeed * ts;
            if (IsKeyPressed(KeyCode::Q))
                transform.Translation.y -= MoveSpeed * ts;
            if (IsKeyPressed(KeyCode::E))
                transform.Translation.y += MoveSpeed * ts;
        }

        void OnDestroy() override {}
    };

} // namespace Engine
