#include "engpch.h"
#include "Script/LuaScriptEngine.h"

#include "Asset/PathUtils.h"
#include "Core/Log.h"
#include "Core/Input.h"
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

        int LuaLogDebug(lua_State* L)
        {
            const char* message = luaL_checkstring(L, 1);
            ENGINE_CORE_TRACE("[Lua] {0}", message ? message : "");
            return 0;
        }

        int LuaIsKeyPressed(lua_State* L)
        {
            int keyCode = static_cast<int>(luaL_checkinteger(L, 1));
            lua_pushboolean(L, Input::IsKeyPressed(static_cast<KeyCode>(keyCode)));
            return 1;
        }

        int LuaIsMouseButtonPressed(lua_State* L)
        {
            int button = static_cast<int>(luaL_checkinteger(L, 1));
            lua_pushboolean(L, Input::IsMouseButtonPressed(static_cast<MouseCode>(button)));
            return 1;
        }

        int LuaGetMousePosition(lua_State* L)
        {
            glm::vec2 pos = Input::GetMousePosition();
            lua_pushnumber(L, pos.x);
            lua_pushnumber(L, pos.y);
            return 2;
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

        int LuaEntityGetRotation(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr) || !entityPtr->HasComponent<TransformComponent>())
            {
                lua_pushnumber(L, 0.0);
                lua_pushnumber(L, 0.0);
                lua_pushnumber(L, 0.0);
                return 3;
            }

            const auto& r = entityPtr->GetComponent<TransformComponent>().Rotation;
            // Convert radians to degrees for usability in Lua
            lua_pushnumber(L, glm::degrees(r.x));
            lua_pushnumber(L, glm::degrees(r.y));
            lua_pushnumber(L, glm::degrees(r.z));
            return 3;
        }

        int LuaEntitySetRotation(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr) || !entityPtr->HasComponent<TransformComponent>())
                return 0;

            float x = static_cast<float>(luaL_checknumber(L, 2));
            float y = static_cast<float>(luaL_checknumber(L, 3));
            float z = static_cast<float>(luaL_checknumber(L, 4));

            auto& tc    = entityPtr->GetComponent<TransformComponent>();
            tc.Rotation = {glm::radians(x), glm::radians(y), glm::radians(z)};
            return 0;
        }

        int LuaEntityGetScale(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr) || !entityPtr->HasComponent<TransformComponent>())
            {
                lua_pushnumber(L, 1.0);
                lua_pushnumber(L, 1.0);
                lua_pushnumber(L, 1.0);
                return 3;
            }

            const auto& s = entityPtr->GetComponent<TransformComponent>().Scale;
            lua_pushnumber(L, s.x);
            lua_pushnumber(L, s.y);
            lua_pushnumber(L, s.z);
            return 3;
        }

        int LuaEntitySetScale(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr) || !entityPtr->HasComponent<TransformComponent>())
                return 0;

            float x = static_cast<float>(luaL_checknumber(L, 2));
            float y = static_cast<float>(luaL_checknumber(L, 3));
            float z = static_cast<float>(luaL_checknumber(L, 4));

            auto& tc = entityPtr->GetComponent<TransformComponent>();
            tc.Scale = {x, y, z};
            return 0;
        }

        int LuaEntityGetForward(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr) || !entityPtr->HasComponent<TransformComponent>())
            {
                lua_pushnumber(L, 0.0);
                lua_pushnumber(L, 0.0);
                lua_pushnumber(L, -1.0);
                return 3;
            }

            const auto& r       = entityPtr->GetComponent<TransformComponent>().Rotation;
            glm::quat   q       = glm::quat({glm::radians(r.x), glm::radians(r.y), glm::radians(r.z)});
            glm::vec3   forward = glm::normalize(-glm::vec3(0, 0, 1) * q);
            lua_pushnumber(L, forward.x);
            lua_pushnumber(L, forward.y);
            lua_pushnumber(L, forward.z);
            return 3;
        }

        int LuaEntityTranslate(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr) || !entityPtr->HasComponent<TransformComponent>())
                return 0;

            float x = static_cast<float>(luaL_checknumber(L, 2));
            float y = static_cast<float>(luaL_checknumber(L, 3));
            float z = static_cast<float>(luaL_checknumber(L, 4));

            auto& tc = entityPtr->GetComponent<TransformComponent>();
            tc.Translation += glm::vec3{x, y, z};
            return 0;
        }

        int LuaEntityRotate(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr) || !entityPtr->HasComponent<TransformComponent>())
                return 0;

            float x = static_cast<float>(luaL_checknumber(L, 2));
            float y = static_cast<float>(luaL_checknumber(L, 3));
            float z = static_cast<float>(luaL_checknumber(L, 4));

            auto& tc = entityPtr->GetComponent<TransformComponent>();
            tc.Rotation += glm::vec3{glm::radians(x), glm::radians(y), glm::radians(z)};
            return 0;
        }

        int LuaEntityGetName(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr))
            {
                lua_pushstring(L, "");
                return 1;
            }

            if (entityPtr->HasComponent<TagComponent>())
            {
                const auto& tag = entityPtr->GetComponent<TagComponent>().Tag;
                lua_pushstring(L, tag.c_str());
            }
            else
            {
                lua_pushstring(L, "Entity");
            }
            return 1;
        }

        int LuaEntityDestroySelf(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr))
                return 0;

            entt::entity entity = static_cast<entt::entity>(*entityPtr);
            Scene*       scene  = entityPtr->GetScene();
            if (scene)
            {
                Entity wrapped = {entity, scene};
                scene->DestroyEntity(wrapped);
            }
            return 0;
        }

        // Scene API - 获取当前实例对应的 Scene 指针
        Scene* GetSceneFromInstance(lua_State* L)
        {
            lua_getfield(L, LUA_REGISTRYINDEX, "LuaScriptInstance_Scene");
            Scene* scene = static_cast<Scene*>(lua_touserdata(L, -1));
            lua_pop(L, 1);
            return scene;
        }

        // 将 Entity 转换为 Lua userdata（带 Engine.Entity metatable）
        void PushEntity(lua_State* L, Entity entity)
        {
            Entity* userdata = static_cast<Entity*>(lua_newuserdatauv(L, sizeof(Entity), 0));
            new (userdata) Entity(entity);
            luaL_getmetatable(L, "Engine.Entity");
            lua_setmetatable(L, -2);
        }

        // 将 UUID 转换为 Entity userdata（Scene 未知时返回 nil）
        void PushEntityByUUID(lua_State* L, UUID uuid, Scene* scene)
        {
            if (uuid == 0)
            {
                lua_pushnil(L);
                return;
            }
            Entity found = scene->FindEntityByUUID(uuid);
            if (!found)
            {
                lua_pushnil(L);
                return;
            }
            PushEntity(L, found);
        }

        // Scene:FindEntityByName(name) - 按名称查找实体
        int LuaSceneFindEntityByName(lua_State* L)
        {
            const char* name  = luaL_checkstring(L, 1);
            Scene*      scene = GetSceneFromInstance(L);
            if (!scene)
            {
                lua_pushnil(L);
                return 1;
            }

            auto view = scene->GetAllEntitiesWith<TagComponent>();
            for (auto entity : view)
            {
                const auto& tag = view.get<TagComponent>(entity).Tag;
                if (tag == name)
                {
                    Entity e = {entity, scene};
                    PushEntity(L, e);
                    return 1;
                }
            }
            lua_pushnil(L);
            return 1;
        }

        // Scene:FindEntitiesWithTag(tag) - 按标签查找所有匹配实体
        int LuaSceneFindEntitiesWithTag(lua_State* L)
        {
            const char* tagName = luaL_checkstring(L, 1);
            Scene*      scene   = GetSceneFromInstance(L);
            if (!scene)
            {
                lua_pushnil(L);
                return 1;
            }

            lua_newtable(L);
            int  index = 1;
            auto view  = scene->GetAllEntitiesWith<TagComponent>();
            for (auto entity : view)
            {
                const auto& tag = view.get<TagComponent>(entity).Tag;
                if (tag == tagName)
                {
                    Entity e = {entity, scene};
                    PushEntity(L, e);
                    lua_seti(L, -2, index++);
                }
            }
            return 1;
        }

        // Scene:GetEntityName(entity) - 获取实体名称
        int LuaSceneGetEntityName(lua_State* L)
        {
            auto*  entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            Scene* scene     = GetSceneFromInstance(L);
            if (!entityPtr || !(*entityPtr) || !scene)
            {
                lua_pushstring(L, "");
                return 1;
            }
            if (entityPtr->HasComponent<TagComponent>())
            {
                const auto& tag = entityPtr->GetComponent<TagComponent>().Tag;
                lua_pushstring(L, tag.c_str());
            }
            else
            {
                lua_pushstring(L, "Entity");
            }
            return 1;
        }

        // Scene:DestroyEntity(entity) - 销毁指定实体
        int LuaSceneDestroyEntity(lua_State* L)
        {
            auto*  entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            Scene* scene     = GetSceneFromInstance(L);
            if (!entityPtr || !(*entityPtr) || !scene)
                return 0;

            entt::entity entity  = static_cast<entt::entity>(*entityPtr);
            Entity       wrapped = {entity, scene};
            scene->DestroyEntity(wrapped);
            return 0;
        }

        // Entity:GetTag() - 获取实体标签
        int LuaEntityGetTag(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr))
            {
                lua_pushstring(L, "");
                return 1;
            }
            if (entityPtr->HasComponent<TagComponent>())
            {
                const auto& tag = entityPtr->GetComponent<TagComponent>().Tag;
                lua_pushstring(L, tag.c_str());
            }
            else
            {
                lua_pushstring(L, "Entity");
            }
            return 1;
        }

        // Entity:SetTag(tag) - 设置实体标签
        int LuaEntitySetTag(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr))
                return 0;
            const char* tag = luaL_checkstring(L, 2);
            if (entityPtr->HasComponent<TagComponent>())
            {
                entityPtr->GetComponent<TagComponent>().Tag = tag;
            }
            return 0;
        }

        // Entity:GetParent() - 获取父实体
        int LuaEntityGetParent(lua_State* L)
        {
            auto*  entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            Scene* scene     = GetSceneFromInstance(L);
            if (!entityPtr || !(*entityPtr) || !scene)
            {
                lua_pushnil(L);
                return 1;
            }
            if (entityPtr->HasComponent<RelationshipComponent>())
            {
                const auto& rc = entityPtr->GetComponent<RelationshipComponent>();
                PushEntityByUUID(L, rc.ParentID, scene);
            }
            else
            {
                lua_pushnil(L);
            }
            return 1;
        }

        // Entity:SetParent(parentEntity) - 设置父实体
        int LuaEntitySetParent(lua_State* L)
        {
            auto*  entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            auto*  parentPtr = static_cast<Entity*>(lua_touserdata(L, 2));
            Scene* scene     = GetSceneFromInstance(L);
            if (!entityPtr || !(*entityPtr) || !scene)
                return 0;

            Entity child = *entityPtr;
            if (!parentPtr || !(*parentPtr))
            {
                scene->RemoveParent(child);
            }
            else
            {
                Entity parent = *parentPtr;
                scene->SetParent(child, parent);
            }
            return 0;
        }

        // Entity:GetChildCount() - 获取子实体数量
        int LuaEntityGetChildCount(lua_State* L)
        {
            auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            if (!entityPtr || !(*entityPtr))
            {
                lua_pushinteger(L, 0);
                return 1;
            }
            if (entityPtr->HasComponent<RelationshipComponent>())
            {
                const auto& rc = entityPtr->GetComponent<RelationshipComponent>();
                lua_pushinteger(L, static_cast<lua_Integer>(rc.Children.size()));
            }
            else
            {
                lua_pushinteger(L, 0);
            }
            return 1;
        }

        // 递归查找子实体
        Entity FindChildRecursive(Scene* scene, entt::entity parent, const std::string& name)
        {
            if (!scene->GetRegistry().any_of<RelationshipComponent>(parent))
                return {};

            const auto& rc = scene->GetRegistry().get<RelationshipComponent>(parent);
            for (UUID childID : rc.Children)
            {
                Entity child = scene->FindEntityByUUID(childID);
                if (child && child.HasComponent<TagComponent>())
                {
                    const auto& tag = child.GetComponent<TagComponent>().Tag;
                    if (tag == name)
                        return child;
                }
                if (child)
                {
                    Entity found = FindChildRecursive(scene, static_cast<entt::entity>(child), name);
                    if (found)
                        return found;
                }
            }
            return {};
        }

        // Entity:FindChildByName(name) - 递归查找子实体
        int LuaEntityFindChildByName(lua_State* L)
        {
            auto*       entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
            const char* name      = luaL_checkstring(L, 2);
            Scene*      scene     = GetSceneFromInstance(L);
            if (!entityPtr || !(*entityPtr) || !scene)
            {
                lua_pushnil(L);
                return 1;
            }

            Entity found = FindChildRecursive(scene, static_cast<entt::entity>(*entityPtr), name);
            if (found)
            {
                PushEntity(L, found);
            }
            else
            {
                lua_pushnil(L);
            }
            return 1;
        }

        void RegisterSceneAPI(lua_State* L)
        {
            lua_newtable(L);
            lua_pushcfunction(L, LuaSceneFindEntityByName);
            lua_setfield(L, -2, "FindEntityByName");
            lua_pushcfunction(L, LuaSceneFindEntitiesWithTag);
            lua_setfield(L, -2, "FindEntitiesWithTag");
            lua_pushcfunction(L, LuaSceneGetEntityName);
            lua_setfield(L, -2, "GetEntityName");
            lua_pushcfunction(L, LuaSceneDestroyEntity);
            lua_setfield(L, -2, "DestroyEntity");
            lua_setglobal(L, "Scene");
        }

        // 向已有 Engine.Entity metatable 添加 Tag/Parent 方法
        // 必须在 RegisterEntityMetatable 之后调用
        void RegisterEntityTagAndParentMethods(lua_State* L)
        {
            luaL_getmetatable(L, "Engine.Entity");
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                return;
            }

            // Tag
            lua_pushcfunction(L, LuaEntityGetTag);
            lua_setfield(L, -2, "GetTag");
            lua_pushcfunction(L, LuaEntitySetTag);
            lua_setfield(L, -2, "SetTag");

            // Parent / Children
            lua_pushcfunction(L, LuaEntityGetParent);
            lua_setfield(L, -2, "GetParent");
            lua_pushcfunction(L, LuaEntitySetParent);
            lua_setfield(L, -2, "SetParent");
            lua_pushcfunction(L, LuaEntityGetChildCount);
            lua_setfield(L, -2, "GetChildCount");
            lua_pushcfunction(L, LuaEntityFindChildByName);
            lua_setfield(L, -2, "FindChildByName");

            lua_pop(L, 1);
        }

        int LuaEntityDistanceTo(lua_State* L)
        {
            auto* selfPtr  = static_cast<Entity*>(lua_touserdata(L, 1));
            auto* otherPtr = static_cast<Entity*>(lua_touserdata(L, 2));

            if (!selfPtr || !(*selfPtr) || !selfPtr->HasComponent<TransformComponent>())
            {
                lua_pushnumber(L, 0.0);
                return 1;
            }

            if (!otherPtr || !(*otherPtr) || !otherPtr->HasComponent<TransformComponent>())
            {
                lua_pushnumber(L, 0.0);
                return 1;
            }

            const auto& a = selfPtr->GetComponent<TransformComponent>().Translation;
            const auto& b = otherPtr->GetComponent<TransformComponent>().Translation;
            lua_pushnumber(L, glm::distance(a, b));
            return 1;
        }

        void RegisterEntityMetatable(lua_State* L)
        {
            if (luaL_newmetatable(L, "Engine.Entity") == 0)
            {
                lua_pop(L, 1);
                return;
            }

            // Transform
            lua_pushcfunction(L, LuaEntityGetTranslation);
            lua_setfield(L, -2, "GetTranslation");
            lua_pushcfunction(L, LuaEntitySetTranslation);
            lua_setfield(L, -2, "SetTranslation");
            lua_pushcfunction(L, LuaEntityGetRotation);
            lua_setfield(L, -2, "GetRotation");
            lua_pushcfunction(L, LuaEntitySetRotation);
            lua_setfield(L, -2, "SetRotation");
            lua_pushcfunction(L, LuaEntityGetScale);
            lua_setfield(L, -2, "GetScale");
            lua_pushcfunction(L, LuaEntitySetScale);
            lua_setfield(L, -2, "SetScale");
            lua_pushcfunction(L, LuaEntityGetForward);
            lua_setfield(L, -2, "GetForward");
            lua_pushcfunction(L, LuaEntityTranslate);
            lua_setfield(L, -2, "Translate");
            lua_pushcfunction(L, LuaEntityRotate);
            lua_setfield(L, -2, "Rotate");

            // Entity
            lua_pushcfunction(L, LuaEntityGetName);
            lua_setfield(L, -2, "GetName");
            lua_pushcfunction(L, LuaEntityDestroySelf);
            lua_setfield(L, -2, "DestroySelf");
            lua_pushcfunction(L, LuaEntityDistanceTo);
            lua_setfield(L, -2, "DistanceTo");

            lua_pushvalue(L, -1);
            lua_setfield(L, -2, "__index");

            lua_pop(L, 1);
        }

        void RegisterEngineGlobal(lua_State* L)
        {
            lua_newtable(L);

            // Logging
            lua_pushcfunction(L, LuaLogInfo);
            lua_setfield(L, -2, "Info");
            lua_pushcfunction(L, LuaLogWarn);
            lua_setfield(L, -2, "Warn");
            lua_pushcfunction(L, LuaLogError);
            lua_setfield(L, -2, "Error");
            lua_pushcfunction(L, LuaLogDebug);
            lua_setfield(L, -2, "Debug");

            // Input
            lua_pushcfunction(L, LuaIsKeyPressed);
            lua_setfield(L, -2, "IsKeyPressed");
            lua_pushcfunction(L, LuaIsMouseButtonPressed);
            lua_setfield(L, -2, "IsMouseButtonPressed");
            lua_pushcfunction(L, LuaGetMousePosition);
            lua_setfield(L, -2, "GetMousePosition");

            lua_setglobal(L, "Engine");

            // Key constants table
            lua_newtable(L);
            // WASD
            lua_pushinteger(L, 0x57);
            lua_setfield(L, -2, "KEY_W");
            lua_pushinteger(L, 0x41);
            lua_setfield(L, -2, "KEY_A");
            lua_pushinteger(L, 0x53);
            lua_setfield(L, -2, "KEY_S");
            lua_pushinteger(L, 0x44);
            lua_setfield(L, -2, "KEY_D");
            // QE
            lua_pushinteger(L, 0x51);
            lua_setfield(L, -2, "KEY_Q");
            lua_pushinteger(L, 0x45);
            lua_setfield(L, -2, "KEY_E");
            // Space / Escape
            lua_pushinteger(L, 0x20);
            lua_setfield(L, -2, "KEY_SPACE");
            lua_pushinteger(L, 0x1B);
            lua_setfield(L, -2, "KEY_ESCAPE");
            // Arrows
            lua_pushinteger(L, 0x26);
            lua_setfield(L, -2, "KEY_UP");
            lua_pushinteger(L, 0x28);
            lua_setfield(L, -2, "KEY_DOWN");
            lua_pushinteger(L, 0x25);
            lua_setfield(L, -2, "KEY_LEFT");
            lua_pushinteger(L, 0x27);
            lua_setfield(L, -2, "KEY_RIGHT");
            // Shift / Ctrl
            lua_pushinteger(L, 0x10);
            lua_setfield(L, -2, "KEY_SHIFT");
            lua_pushinteger(L, 0x11);
            lua_setfield(L, -2, "KEY_CTRL");
            // Functional keys
            lua_pushinteger(L, 0x70);
            lua_setfield(L, -2, "KEY_F1");
            lua_pushinteger(L, 0x71);
            lua_setfield(L, -2, "KEY_F2");
            lua_pushinteger(L, 0x72);
            lua_setfield(L, -2, "KEY_F3");
            lua_pushinteger(L, 0x73);
            lua_setfield(L, -2, "KEY_F4");
            lua_pushinteger(L, 0x74);
            lua_setfield(L, -2, "KEY_F5");
            lua_pushinteger(L, 0x75);
            lua_setfield(L, -2, "KEY_F6");
            lua_pushinteger(L, 0x76);
            lua_setfield(L, -2, "KEY_F7");
            lua_pushinteger(L, 0x77);
            lua_setfield(L, -2, "KEY_F8");
            lua_pushinteger(L, 0x78);
            lua_setfield(L, -2, "KEY_F9");
            lua_pushinteger(L, 0x79);
            lua_setfield(L, -2, "KEY_F10");
            lua_pushinteger(L, 0x7A);
            lua_setfield(L, -2, "KEY_F11");
            lua_pushinteger(L, 0x7B);
            lua_setfield(L, -2, "KEY_F12");
            lua_setglobal(L, "Key");

            // Mouse constants
            lua_newtable(L);
            lua_pushinteger(L, 0);
            lua_setfield(L, -2, "MOUSE_LEFT");
            lua_pushinteger(L, 1);
            lua_setfield(L, -2, "MOUSE_RIGHT");
            lua_pushinteger(L, 2);
            lua_setfield(L, -2, "MOUSE_MIDDLE");
            lua_setglobal(L, "Mouse");
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
        RegisterEngineGlobal(m_State);
        RegisterEntityMetatable(m_State);
        RegisterEntityTagAndParentMethods(m_State);
        RegisterSceneAPI(m_State);

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
        instance.ScenePtr   = scene;
        instance.TableRef   = luaL_ref(m_State, LUA_REGISTRYINDEX);
        m_Instances[entity] = instance;

        // 将 scene 指针存入 Lua registry，供 Scene API 函数使用
        lua_pushlightuserdata(m_State, scene);
        lua_setfield(m_State, LUA_REGISTRYINDEX, "LuaScriptInstance_Scene");

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
