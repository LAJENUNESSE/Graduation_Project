#include "EditorShell.h"

#include "Core/Input.h"
#include "Core/KeyCodes.h"

namespace Engine
{

    EditorShellActions EditorShell::Draw(const EditorShellState& state)
    {
        EditorShellActions actions;

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace", &m_DockspaceOpen, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), m_DockspaceFlags);
        }

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("文件"))
            {
                if (ImGui::MenuItem("新建场景", "Ctrl+N"))
                    actions.RequestNewScene = true;
                if (ImGui::MenuItem("打开场景...", "Ctrl+O"))
                    actions.RequestOpenScene = true;
                if (ImGui::MenuItem("保存场景...", "Ctrl+Shift+S", false, state.CanSave))
                    actions.RequestSaveScene = true;
                ImGui::Separator();
                if (ImGui::MenuItem("退出"))
                    actions.RequestCloseApplication = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("编辑"))
            {
                std::string undoLabel = "撤销";
                if (!state.UndoDescription.empty())
                    undoLabel += " (" + state.UndoDescription + ")";
                if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, state.CanUndo))
                    actions.RequestUndo = true;

                std::string redoLabel = "重做";
                if (!state.RedoDescription.empty())
                    redoLabel += " (" + state.RedoDescription + ")";
                if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, state.CanRedo))
                    actions.RequestRedo = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("视图"))
            {
                if (ImGui::MenuItem("性能监控", nullptr, state.ShowStatsPanel))
                    actions.ToggleStatsPanel = true;
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        ImGui::End();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));

        ImGui::Begin("##工具栏", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        float windowWidth = ImGui::GetWindowContentRegionMax().x;
        constexpr float buttonWidth = 80.0f;
        constexpr float buttonHeight = 28.0f;

        ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
        ImGui::SetCursorPosY((ImGui::GetWindowHeight() - buttonHeight) * 0.5f);

        if (state.CurrentSceneState == SceneState::Edit)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
            if (ImGui::Button("▶ 播放", ImVec2(buttonWidth, buttonHeight)))
                actions.RequestPlay = true;
            ImGui::PopStyleColor(3);
        }
        else if (state.CurrentSceneState == SceneState::Play)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("■ 停止", ImVec2(buttonWidth, buttonHeight)))
                actions.RequestStop = true;
            ImGui::PopStyleColor(3);
        }

        ImGui::PopStyleVar(2);
        ImGui::End();

        return actions;
    }

    EditorShellActions EditorShell::OnKeyPressed(const KeyPressedEvent& e, const EditorShellState& state) const
    {
        EditorShellActions actions;
        if (e.GetRepeatCount() > 0)
            return actions;

        bool control = Input::IsKeyPressed(KeyCode::LeftControl) || Input::IsKeyPressed(KeyCode::RightControl);
        bool shift = Input::IsKeyPressed(KeyCode::LeftShift) || Input::IsKeyPressed(KeyCode::RightShift);

        switch (static_cast<KeyCode>(e.GetKeyCode()))
        {
        case KeyCode::N:
            if (control)
                actions.RequestNewScene = true;
            break;
        case KeyCode::O:
            if (control)
                actions.RequestOpenScene = true;
            break;
        case KeyCode::S:
            if (control && shift && state.CanSave)
                actions.RequestSaveScene = true;
            break;
        case KeyCode::Z:
            if (control && state.CanUndo)
                actions.RequestUndo = true;
            break;
        case KeyCode::Y:
            if (control && state.CanRedo)
                actions.RequestRedo = true;
            break;
        default:
            break;
        }

        return actions;
    }

} // namespace Engine
