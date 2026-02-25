#pragma once

#include "Script/ScriptableEntity.h"

namespace Engine
{

    class RotateScript : public ScriptableEntity
    {
    public:
        float RotateSpeed = 1.0f;

        void OnCreate() override {}

        void OnUpdate(Timestep ts) override
        {
            auto& transform = GetTransform();
            transform.Rotation.y += RotateSpeed * ts;
        }

        void OnDestroy() override {}
    };

} // namespace Engine
