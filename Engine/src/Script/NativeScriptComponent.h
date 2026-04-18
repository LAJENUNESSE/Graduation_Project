#pragma once

#include "Script/ScriptableEntity.h"
#include <functional>
#include <memory>
#include <string>

namespace Engine
{

    enum class ScriptBackend
    {
        NativeCpp = 0,
        Lua
    };

    struct NativeScriptComponent
    {
        ScriptBackend                     Backend = ScriptBackend::NativeCpp;
        std::string                       ScriptName;
        std::string                       ScriptPath;
        std::shared_ptr<ScriptableEntity> Instance;

        std::function<void(NativeScriptComponent&)> InstantiateScript;
        std::function<void(NativeScriptComponent&)> DestroyScript;

        NativeScriptComponent()                             = default;
        NativeScriptComponent(const NativeScriptComponent&) = default;
    };

} // namespace Engine
