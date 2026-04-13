#include "engpch.h"
#include "Script/LuaScriptEngine.h"

#include "Asset/PathUtils.h"
#include "Core/Log.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace Engine
{

    namespace
    {
        int LuaLogInfo(lua_State* L)
        {
            const char* message = luaL_checkstring(L, 1);
            ENGINE_CORE_INFO("[Lua] {0}", message ? message : "");
            return 0;
        }

        int LuaLogWarn(lua_State* L)
        {
            const char* message = luaL_checkstring(L, 1);
            ENGINE_CORE_WARN("[Lua] {0}", message ? message : "");
            return 0;
        }

        int LuaLogError(lua_State* L)
        {
            const char* message = luaL_checkstring(L, 1);
            ENGINE_CORE_ERROR("[Lua] {0}", message ? message : "");
            return 0;
        }

        int LuaEntityGetTranslation(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr) || !entityPtr->HasComponent<TransformComponent>())
            {
                lua_pushnumber(L, 0.0);
                lua_pushnumber(L, 0.0);
                lua_pushnumber(L, 0.0);
                return 3;
            }

            const auto& t = entityPtr->GetComponent<TransformComponent>().Translation;
            lua_pushnumber(L, t.x);
            lua_pushnumber(L, t.y);
            lua_pushnumber(L, t.z);
            return 3;
        }

        int LuaEntitySetTranslation(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr) || !entityPtr->HasComponent<TransformComponent>())
                return 0;

            float x = static_cast<float>(luaL_checknumber(L, 2));
            float y = static_cast<float>(luaL_checknumber(L, 3));
            float z = static_cast<float>(luaL_checknumber(L, 4));

            auto& tc       = entityPtr->GetComponent<TransformComponent>();
            tc.Translation = {x, y, z};
            return 0;
        }

        void RegisterEntityMetatable(lua_State* L)
        {
            if (luaL_newmetatable(L, "Engine.Entity") == 0)
            {
                lua_pop(L, 1);
                return;
            }

            lua_pushcfunction(L, LuaEntityGetTranslation);
            lua_setfield(L, -2, "GetTranslation");

            lua_pushcfunction(L, LuaEntitySetTranslation);
            lua_setfield(L, -2, "SetTranslation");

            lua_pushvalue(L, -1);
            lua_setfield(L, -2, "__index");

            lua_pop(L, 1);
        }
    } // namespace

    LuaScriptEngine::LuaScriptEngine() = default;

    LuaScriptEngine::~LuaScriptEngine()
    {
        Shutdown();
    }

    bool LuaScriptEngine::Init()
    {
        if (m_State)
            return true;

        m_State = luaL_newstate();
        if (!m_State)
        {
            ENGINE_CORE_ERROR("[Lua] Failed to create lua_State");
            return false;
        }

        luaL_openlibs(m_State);

        lua_newtable(m_State);
        lua_pushcfunction(m_State, LuaLogInfo);
        lua_setfield(m_State, -2, "Info");
        lua_pushcfunction(m_State, LuaLogWarn);
        lua_setfield(m_State, -2, "Warn");
        lua_pushcfunction(m_State, LuaLogError);
        lua_setfield(m_State, -2, "Error");
        lua_setglobal(m_State, "Engine");

        RegisterEntityMetatable(m_State);

        return true;
    }

    void LuaScriptEngine::Shutdown()
    {
        if (!m_State)
            return;

        for (auto& [_, instance] : m_Instances)
        {
            if (instance.TableRef != LUA_NOREF && instance.TableRef != LUA_REFNIL)
                luaL_unref(m_State, LUA_REGISTRYINDEX, instance.TableRef);
        }
        m_Instances.clear();

        lua_close(m_State);
        m_State = nullptr;
    }

    void LuaScriptEngine::CreateEntityInstance(entt::entity entity, Scene* scene, const std::string& scriptPath)
    {
        if (!m_State)
            return;

        if (scriptPath.empty())
            return;

        std::filesystem::path resolved = PathUtils::ResolvePath(scriptPath);
        if (!std::filesystem::exists(resolved))
        {
            ENGINE_CORE_ERROR("[Lua] Script not found: {0}", scriptPath);
            return;
        }

        const std::string absPath = PathUtils::PathToUtf8String(resolved);
        if (luaL_dofile(m_State, absPath.c_str()) != LUA_OK)
        {
            const char* error = lua_tostring(m_State, -1);
            ENGINE_CORE_ERROR("[Lua] Load failed ({0}): {1}", scriptPath, error ? error : "unknown");
            lua_pop(m_State, 1);
            return;
        }

        if (!lua_istable(m_State, -1))
        {
            ENGINE_CORE_ERROR("[Lua] Script must return a table: {0}", scriptPath);
            lua_pop(m_State, 1);
            return;
        }

        auto it = m_Instances.find(entity);
        if (it != m_Instances.end())
        {
            if (it->second.TableRef != LUA_NOREF && it->second.TableRef != LUA_REFNIL)
                luaL_unref(m_State, LUA_REGISTRYINDEX, it->second.TableRef);
            m_Instances.erase(it);
        }

        ScriptInstance instance;
        instance.ScriptPath = scriptPath;
        instance.TableRef   = luaL_ref(m_State, LUA_REGISTRYINDEX);
        m_Instances[entity] = instance;

        // 注入 Entity userdata
        auto& created = m_Instances[entity];
        lua_rawgeti(m_State, LUA_REGISTRYINDEX, created.TableRef);
        Entity* userdata = static_cast<Entity*>(lua_newuserdatauv(m_State, sizeof(Entity), 0));
        new (userdata) Entity(entity, scene);

        luaL_getmetatable(m_State, "Engine.Entity");
        lua_setmetatable(m_State, -2);
        lua_setfield(m_State, -2, "Entity");
        lua_pop(m_State, 1);

        CallMethod(entity, "OnCreate");
    }

    void LuaScriptEngine::DestroyEntityInstance(entt::entity entity)
    {
        auto it = m_Instances.find(entity);
        if (it == m_Instances.end())
            return;

        CallMethod(entity, "OnDestroy");

        if (m_State && it->second.TableRef != LUA_NOREF && it->second.TableRef != LUA_REFNIL)
            luaL_unref(m_State, LUA_REGISTRYINDEX, it->second.TableRef);

        m_Instances.erase(it);
    }

    void LuaScriptEngine::UpdateEntity(entt::entity entity, Timestep ts)
    {
        CallMethod(entity, "OnUpdate", ts);
    }

    bool LuaScriptEngine::CallMethod(entt::entity entity, const char* methodName, Timestep ts)
    {
        if (!m_State)
            return false;

        auto it = m_Instances.find(entity);
        if (it == m_Instances.end())
            return false;

        lua_rawgeti(m_State, LUA_REGISTRYINDEX, it->second.TableRef);
        if (!lua_istable(m_State, -1))
        {
            lua_pop(m_State, 1);
            return false;
        }

        lua_getfield(m_State, -1, methodName);
        if (!lua_isfunction(m_State, -1))
        {
            lua_pop(m_State, 2);
            return false;
        }

        lua_pushvalue(m_State, -2); // self

        int argCount = 1;
        if (std::string(methodName) == "OnUpdate")
        {
            lua_pushnumber(m_State, static_cast<lua_Number>(ts.GetSeconds()));
            argCount = 2;
        }

        if (lua_pcall(m_State, argCount, 0, 0) != LUA_OK)
        {
            const char* error = lua_tostring(m_State, -1);
            ENGINE_CORE_ERROR("[Lua] {0} failed: {1}", methodName, error ? error : "unknown");
            lua_pop(m_State, 2); // err + table
            return false;
        }

        lua_pop(m_State, 1); // table
        return true;
    }

} // namespace Engine
