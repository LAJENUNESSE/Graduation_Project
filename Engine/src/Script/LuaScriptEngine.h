#pragma once

#include "Core/Timestep.h"

#include <entt/entt.hpp>

#include <memory>
#include <string>
#include <unordered_map>

struct lua_State;

namespace Engine
{

    class Scene;

    class LuaScriptEngine
    {
    public:
        LuaScriptEngine();
        ~LuaScriptEngine();

        bool Init();
        void Shutdown();

        void CreateEntityInstance(entt::entity entity, Scene* scene, const std::string& scriptPath);
        void DestroyEntityInstance(entt::entity entity);
        void UpdateEntity(entt::entity entity, Timestep ts);

    private:
        struct ScriptInstance
        {
            int         TableRef = 0;
            std::string ScriptPath;
        };

        bool CallMethod(entt::entity entity, const char* methodName, Timestep ts = Timestep(0.0f));

        lua_State*                                       m_State = nullptr;
        std::unordered_map<entt::entity, ScriptInstance> m_Instances;
    };

} // namespace Engine
