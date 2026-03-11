#pragma once

#include "Reflection/ComponentMeta.h"
#include <vector>

namespace Engine
{

    class ComponentRegistry
    {
    public:
        static ComponentRegistry& Instance()
        {
            static ComponentRegistry s_Instance;
            return s_Instance;
        }

        void Register(const ComponentMeta& meta) { m_Metas.push_back(meta); }

        const std::vector<ComponentMeta>& GetAll() const { return m_Metas; }

        const ComponentMeta* Find(const char* typeName) const
        {
            for (auto& m : m_Metas)
                if (std::string(m.TypeName) == typeName)
                    return &m;
            return nullptr;
        }

        // 确保反射注册被链接（静态库需显式调用）
        static void EnsureRegistered();

    private:
        ComponentRegistry() = default;
        std::vector<ComponentMeta> m_Metas;
    };

} // namespace Engine
