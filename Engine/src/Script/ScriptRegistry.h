#pragma once

#include "Script/NativeScriptComponent.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace Engine
{

    struct ScriptEntry
    {
        const char*                                        DisplayName;
        std::function<std::shared_ptr<ScriptableEntity>()> Factory;
    };

    class ScriptRegistry
    {
    public:
        static ScriptRegistry& Instance()
        {
            static ScriptRegistry s_Instance;
            return s_Instance;
        }

        void Register(const std::string&                                 name,
                      const char*                                        displayName,
                      std::function<std::shared_ptr<ScriptableEntity>()> factory)
        {
            m_Scripts[name] = {displayName, std::move(factory)};
        }

        void Bind(NativeScriptComponent& nsc, const std::string& name)
        {
            auto it = m_Scripts.find(name);
            if (it == m_Scripts.end())
                return;

            nsc.ScriptName        = name;
            auto factory          = it->second.Factory;
            nsc.InstantiateScript = [factory](NativeScriptComponent& comp) { comp.Instance = factory(); };
            nsc.DestroyScript     = [](NativeScriptComponent& comp) { comp.Instance.reset(); };
        }

        const std::unordered_map<std::string, ScriptEntry>& GetAll() const { return m_Scripts; }

    private:
        ScriptRegistry() = default;
        std::unordered_map<std::string, ScriptEntry> m_Scripts;
    };

} // namespace Engine

// 注册脚本宏
#define REGISTER_SCRIPT(ScriptClass, displayName)                                                                      \
    static bool s_Registered_##ScriptClass = []() -> bool                                                              \
    {                                                                                                                  \
        ::Engine::ScriptRegistry::Instance().Register(#ScriptClass, displayName,                                       \
                                                      []() -> std::shared_ptr<::Engine::ScriptableEntity>              \
                                                      { return std::make_shared<ScriptClass>(); });                    \
        return true;                                                                                                   \
    }();
