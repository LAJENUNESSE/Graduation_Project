#pragma once

#include "Core/Base.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/PostProcessing.h"

#include <functional>

namespace Engine
{

    class SceneRenderer;
    class Scene;

    class RenderSettingsPanel
    {
    public:
        RenderSettingsPanel() = default;

        void SetContext(SceneRenderer* sceneRenderer, PostProcessingSettings* postProcessingSettings,
                        Ref<Framebuffer> hdrFramebuffer, Ref<Scene> scene, bool* showPhysicsColliders);

        void SetScene(const Ref<Scene>& scene) { m_Scene = scene; }
        void SetHDRFramebuffer(const Ref<Framebuffer>& fb) { m_HDRFramebuffer = fb; }
        void SetReadOnly(bool readOnly) { m_ReadOnly = readOnly; }

        using MSAAChangedCallback = std::function<void(uint32_t samples)>;
        void SetMSAAChangedCallback(MSAAChangedCallback callback) { m_OnMSAAChanged = std::move(callback); }

        void OnImGuiRender();

    private:
        SceneRenderer* m_SceneRenderer = nullptr;
        PostProcessingSettings* m_PostProcessingSettings = nullptr;
        Ref<Framebuffer> m_HDRFramebuffer;
        Ref<Scene> m_Scene;
        bool* m_ShowPhysicsColliders = nullptr;
        MSAAChangedCallback m_OnMSAAChanged;
        bool m_ReadOnly = false;
    };

} // namespace Engine
