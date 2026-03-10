#pragma once

#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Reflection/ComponentMeta.h"
#include "Reflection/AutoInspector.h"

namespace Engine
{
    class CommandHistory;

    class PropertiesPanel
    {
    public:
        struct Vec3ControlEditState
        {
            bool ValueChanged = false;
            bool EditStarted = false;
            bool EditFinished = false;
        };

        PropertiesPanel() = default;

        void OnImGuiRender(Entity selectedEntity);
        void SetCommandHistory(CommandHistory* commandHistory) { m_CommandHistory = commandHistory; }

        // 公开给 AutoInspector 适配器使用
        Vec3ControlEditState DrawVec3Control(const std::string& label, glm::vec3& values,
                                             float resetValue = 0.0f, float columnWidth = 100.0f);

    private:
        struct TransformEditSession
        {
            bool Active = false;
            UUID EntityID = 0;
            Scene* SceneContext = nullptr;
            glm::vec3 Translation = {};
            glm::vec3 Rotation = {};
            glm::vec3 Scale = {1.0f, 1.0f, 1.0f};
        };

        void DrawComponents(Entity entity);

        template <typename T, typename UIFunction>
        void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction, bool removable = true);

        // 反射组件通用绘制（类型擦除版）
        void DrawComponent_Auto(const std::string& name, Entity entity,
                                const ComponentMeta& meta, AutoInspector::DrawVec3Fn drawVec3);

    private:
        CommandHistory* m_CommandHistory = nullptr;
        TransformEditSession m_TransformEditSession;
    };

} // namespace Engine