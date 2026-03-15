#pragma once

#include "Core/Base.h"
#include "EditorSceneSession.h"
#include "Events/KeyEvent.h"

#include <imgui.h>

#include <string>

namespace Engine
{

    struct EditorShellState
    {
        SceneState CurrentSceneState = SceneState::Edit;
        bool CanSave = true;
        bool CanUndo = false;
        bool CanRedo = false;
        std::string UndoDescription;
        std::string RedoDescription;
        bool ShowStatsPanel = true;
    };

    struct EditorShellActions
    {
        bool RequestNewScene = false;
        bool RequestOpenScene = false;
        bool RequestSaveScene = false;
        bool RequestPlay = false;
        bool RequestStop = false;
        bool RequestUndo = false;
        bool RequestRedo = false;
        bool ToggleStatsPanel = false;
        bool RequestCloseApplication = false;
    };

    class EditorShell
    {
    public:
        EditorShellActions Draw(const EditorShellState& state);
        EditorShellActions OnKeyPressed(const KeyPressedEvent& e, const EditorShellState& state) const;

    private:
        bool m_DockspaceOpen = true;
        ImGuiDockNodeFlags m_DockspaceFlags = ImGuiDockNodeFlags_None;
    };

} // namespace Engine
