#pragma once

#include "Scene/Entity.h"
#include "Scene/Components.h"

namespace Engine
{

    class PropertiesPanel
    {
    public:
        PropertiesPanel() = default;

        void OnImGuiRender(Entity selectedEntity);

    private:
        void DrawComponents(Entity entity);

        template <typename T, typename UIFunction>
        void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction, bool removable = true);

        void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f,
                             float columnWidth = 100.0f);
    };

} // namespace Engine
