#pragma once

#include "Core/Layer.h"

#include <glm/vec4.hpp>

namespace Engine
{

    class VulkanSmokeLayer : public Layer
    {
    public:
        VulkanSmokeLayer();

        void OnAttach() override;
        void OnUpdate(Timestep ts) override;

    private:
        glm::vec4 m_ClearColor = {0.08f, 0.10f, 0.14f, 1.0f};
    };

} // namespace Engine
