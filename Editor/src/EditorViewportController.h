#pragma once

#include "Core/Base.h"
#include "Core/Timestep.h"
#include "Events/Event.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/Framebuffer.h"

#include <glm/glm.hpp>

namespace Engine
{

    class Scene;

    struct EditorViewportContext
    {
        glm::vec2 Size = {1280.0f, 720.0f};
        glm::vec2 Bounds[2] = {};
        bool Focused = false;
        bool Hovered = false;
    };

    class EditorViewportController
    {
    public:
        void Initialize(uint32_t width = 1280, uint32_t height = 720);
        void OnUpdate(Timestep ts, Scene& activeScene);
        void OnEvent(Event& event);

        EditorViewportContext BeginViewportWindow();
        void EndViewportWindow();

        const Ref<Framebuffer>& GetFramebuffer() const { return m_Framebuffer; }
        const Ref<Framebuffer>& GetHDRFramebuffer() const { return m_HDRFramebuffer; }
        void SetHDRFramebuffer(const Ref<Framebuffer>& framebuffer) { m_HDRFramebuffer = framebuffer; }
        EditorCamera& GetCamera() { return m_EditorCamera; }
        const EditorViewportContext& GetContext() const { return m_Context; }
        void ApplyMSAASamples(uint32_t samples);

    private:
        Ref<Framebuffer> m_Framebuffer;
        Ref<Framebuffer> m_HDRFramebuffer;
        EditorCamera m_EditorCamera;
        EditorViewportContext m_Context;
    };

} // namespace Engine
