#pragma once

#include "Core/Timestep.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

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

        // 碰撞/触发器回调分发（供 SceneRuntimeCoordinator 调用）
        // entity: 当前脚本所属实体
        // otherEntity: 碰撞的对方实体
        void DispatchCollisionEnter(entt::entity     entity,
                                    entt::entity     otherEntity,
                                    const glm::vec3& contactPoint,
                                    const glm::vec3& contactNormal,
                                    float            impulse);
        void DispatchCollisionStay(entt::entity     entity,
                                   entt::entity     otherEntity,
                                   const glm::vec3& contactPoint,
                                   const glm::vec3& contactNormal,
                                   float            impulse);
        void DispatchCollisionExit(entt::entity entity, entt::entity otherEntity);
        void DispatchTriggerEnter(entt::entity entity, entt::entity otherEntity);
        void DispatchTriggerExit(entt::entity entity, entt::entity otherEntity);

    private:
        struct ScriptInstance
        {
            int         TableRef = 0;
            std::string ScriptPath;
            Scene*      ScenePtr = nullptr;
        };

        bool CallMethod(entt::entity entity, const char* methodName, Timestep ts = Timestep(0.0f));

        lua_State*                                       m_State = nullptr;
        std::unordered_map<entt::entity, ScriptInstance> m_Instances;
    };

} // namespace Engine
