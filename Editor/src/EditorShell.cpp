#include "EditorShell.h"

#include "Core/Input.h"
#include "Core/KeyCodes.h"

#include <imgui_internal.h>

namespace Engine
{

    EditorShellActions EditorShell::Draw(const EditorShellState& state)
    {
        EditorShellActions actions;

        ImGuiWindowFlags     windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        const ImGuiViewport* viewport    = ImGui::GetMainViewport();
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

            ImGuiDockNodeFlags dockspaceFlags = m_DockspaceFlags;
            if (state.IsLayoutLocked)
            {
                // To lock the layout, we disable splitting and resizing. Note: this applies to the root dockspace node,
                // and child nodes inherit it, but some individual windows might need NoMove flags as well.
                dockspaceFlags |= ImGuiDockNodeFlags_NoSplit | ImGuiDockNodeFlags_NoResize;
            }

            if (m_ResetLayoutRequested)
            {
                m_ResetLayoutRequested = false;
                ImGui::DockBuilderRemoveNode(dockspaceId);
                ImGui::DockBuilderAddNode(dockspaceId, dockspaceFlags | ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

                auto dockMainId  = dockspaceId;
                auto dockRightId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, nullptr, &dockMainId);
                auto dockBottomId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.30f, nullptr, &dockMainId);
                auto dockRightBottomId =
                    ImGui::DockBuilderSplitNode(dockRightId, ImGuiDir_Down, 0.50f, nullptr, &dockRightId);

                // Main Area -> Viewport
                ImGui::DockBuilderDockWindow("\xe8\xa7\x86\xe5\x8f\xa3", dockMainId); // 视口

                // Right Top -> Hierarchy
                ImGui::DockBuilderDockWindow("\xe5\x9c\xba\xe6\x99\xaf\xe5\xb1\x82\xe7\xba\xa7",
                                             dockRightId); // 场景层级

                // Right Bottom -> Properties, Render Settings
                ImGui::DockBuilderDockWindow("\xe5\xb1\x9e\xe6\x80\xa7", dockRightBottomId); // 属性
                ImGui::DockBuilderDockWindow("\xe6\xb8\xb2\xe6\x9f\x93\xe8\xae\xbe\xe7\xbd\xae",
                                             dockRightBottomId); // 渲染设置

                // Bottom -> Asset Browser, Console
                ImGui::DockBuilderDockWindow("\xe8\xb5\x84\xe4\xba\xa7\xe6\xb5\x8f\xe8\xa7\x88\xe5\x99\xa8",
                                             dockBottomId);                                         // 资产浏览器
                ImGui::DockBuilderDockWindow("\xe6\x8e\xa7\xe5\x88\xb6\xe5\x8f\xb0", dockBottomId); // 控制台

                // Main Area -> Script Editor (和视口同 tab 组，默认隐藏在后面)
                ImGui::DockBuilderDockWindow("\xe8\x84\x9a\xe6\x9c\xac\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8",
                                             dockMainId); // 脚本编辑器

                ImGui::DockBuilderFinish(dockspaceId);
            }

            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
        }

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("文件"))
            {
                if (ImGui::MenuItem("新建场景", "Ctrl+N"))
                    actions.RequestNewScene = true;
                if (ImGui::MenuItem("打开场景...", "Ctrl+O"))
                    actions.RequestOpenScene = true;
                if (ImGui::MenuItem("保存场景", "Ctrl+S", false, state.CanSave))
                    actions.RequestSaveSceneQuick = true;
                if (ImGui::MenuItem("场景另存为...", "Ctrl+Shift+S", false, state.CanSave))
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
                if (ImGui::MenuItem("恢复默认布局 (UE风格)"))
                    actions.RequestResetLayout = true;

                if (ImGui::MenuItem("锁定面板布局", nullptr, state.IsLayoutLocked))
                    actions.ToggleLayoutLock = true;

                ImGui::Separator();

                if (ImGui::MenuItem("FPS 叠加显示", nullptr, state.ShowFpsOverlay))
                    actions.ToggleFpsOverlay = true;
                if (ImGui::MenuItem("性能监控", nullptr, state.ShowStatsPanel))
                    actions.ToggleStatsPanel = true;
                if (ImGui::MenuItem("脚本编辑器", nullptr, state.ShowScriptEditorPanel))
                    actions.ToggleScriptEditorPanel = true;
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        ImGui::End();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(4.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));

        ImGui::Begin("##toolbar", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        constexpr float buttonHeight = 32.0f;
        const float     winHeight    = ImGui::GetWindowHeight();
        const float     centerY      = (winHeight - buttonHeight) * 0.5f;

        // ---- 左组：模式占位 ----
        ImGui::SetCursorPosY(centerY);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::Button("\xe9\x80\x89\xe6\x8b\xa9\xe6\xa8\xa1\xe5\xbc\x8f \xe2\x96\xbc", ImVec2(96.0f, buttonHeight));
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::SetCursorPosY(centerY);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // ---- 中组：播放 / 停止（居中）----
        const float     playBtnWidth = 88.0f;
        const float     windowWidth  = ImGui::GetWindowContentRegionMax().x;
        const float     centerX      = (windowWidth - playBtnWidth) * 0.5f;
        constexpr float kLeftGroupW  = 96.0f + 8.0f + 1.0f + 8.0f; // 近似左组宽度
        const float     finalX       = centerX > kLeftGroupW ? centerX : kLeftGroupW + 8.0f;
        ImGui::SetCursorPosX(finalX);
        ImGui::SetCursorPosY(centerY);

        if (state.CurrentSceneState == SceneState::Edit)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.50f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.62f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.40f, 0.12f, 1.0f));
            if (ImGui::Button("\xe2\x96\xb6 \xe6\x92\xad\xe6\x94\xbe", ImVec2(playBtnWidth, buttonHeight)))
                actions.RequestPlay = true;
            ImGui::PopStyleColor(3);
        }
        else if (state.CurrentSceneState == SceneState::Play)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.16f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.22f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.12f, 0.12f, 1.0f));
            if (ImGui::Button("\xe2\x96\xa0 \xe5\x81\x9c\xe6\xad\xa2", ImVec2(playBtnWidth, buttonHeight)))
                actions.RequestStop = true;
            ImGui::PopStyleColor(3);
        }

        // ---- 右组：设置占位 ----
        ImGui::SameLine();
        const float settingsBtnWidth = 64.0f;
        ImGui::SetCursorPosX(windowWidth - settingsBtnWidth - 4.0f);
        ImGui::SetCursorPosY(centerY);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        ImGui::SetCursorPosY(centerY);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::Button("\xe8\xae\xbe\xe7\xbd\xae \xe2\x96\xbc", ImVec2(settingsBtnWidth, buttonHeight));
        ImGui::PopStyleColor(3);

        ImGui::PopStyleVar(3);
        ImGui::End();

        return actions;
    }

    EditorShellActions EditorShell::OnKeyPressed(const KeyPressedEvent& e, const EditorShellState& state) const
    {
        EditorShellActions actions;
        if (e.GetRepeatCount() > 0)
            return actions;

        bool control = Input::IsKeyPressed(KeyCode::LeftControl) || Input::IsKeyPressed(KeyCode::RightControl);
        bool shift   = Input::IsKeyPressed(KeyCode::LeftShift) || Input::IsKeyPressed(KeyCode::RightShift);

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
            else if (control && state.CanSave)
                actions.RequestSaveSceneQuick = true;
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
