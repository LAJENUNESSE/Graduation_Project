#pragma once

#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Reflection/ComponentMeta.h"
#include "Reflection/AutoInspector.h"

namespace Engine
{

    class PropertiesPanel
    {
    public:
        PropertiesPanel() = default;

        void OnImGuiRender(Entity selectedEntity);

        // 公开给 AutoInspector 适配器使用
        void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f,
                             float columnWidth = 100.0f);

    private:
        void DrawComponents(Entity entity);

        template <typename T, typename UIFunction>
        void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction, bool removable = true);

        // 反射组件通用绘制（类型擦除版）
        void DrawComponent_Auto(const std::string& name, Entity entity,
                                const ComponentMeta& meta, AutoInspector::DrawVec3Fn drawVec3);
    };

} // namespace Engine
