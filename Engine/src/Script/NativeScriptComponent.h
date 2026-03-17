#pragma once

#include "Script/ScriptableEntity.h"
#include <functional>
#include <memory>
#include <string>

namespace Engine
{

    struct NativeScriptComponent
    {
        std::string                       ScriptName;
        std::shared_ptr<ScriptableEntity> Instance;

        std::function<void(NativeScriptComponent&)> InstantiateScript;
        std::function<void(NativeScriptComponent&)> DestroyScript;

        NativeScriptComponent()                             = default;
        NativeScriptComponent(const NativeScriptComponent&) = default;
    };

} // namespace Engine
