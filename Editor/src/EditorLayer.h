#pragma once

#include "Core/Base.h"
#include "Core/Layer.h"
#include "Scene/SceneSerializer.h"

#include <optional>
#include <string>

namespace Engine
{
    class Scene;
    class EditorBootstrapper;
    struct EditorShellActions;
    struct EditorShellState;
    class KeyPressedEvent;
    class MouseButtonPressedEvent;

    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        ~EditorLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Timestep ts) override;
        void OnImGuiRender() override;
        void OnEvent(Event& event) override;

    private:
        bool OnKeyPressed(KeyPressedEvent& e);
        bool OnMouseButtonPressed(MouseButtonPressedEvent& e);

        void NewScene();
        void OpenScene();
        void OpenScene(const std::string& filepath);
        void SaveScene();

        void OnScenePlay();
        void OnSceneStop();
        void HandleShellActions(const EditorShellActions& actions);
        EditorShellState BuildShellState() const;

        void BootstrapDefaultScene();
        void ApplyActiveSceneContext(bool clearCommandHistory);
        void SyncCommandHistorySuspension();
        void RestoreEditorRenderSettingsSnapshot();

    private:
        Scope<EditorBootstrapper> m_Boot;
        Ref<Scene> m_ActiveScene;
        std::optional<EditorRenderSettings> m_PrePlayRenderSettings;
    };

} // namespace Engine
