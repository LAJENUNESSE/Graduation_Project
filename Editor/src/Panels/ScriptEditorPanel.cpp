#include "ScriptEditorPanel.h"

#include "ImGui/ImGuiLayer.h"

#include <TextEditor.h>
#include <imgui.h>

namespace Engine
{

    namespace
    {
        // 阶段 1 占位内容，阶段 2 文件打开功能接入后即被覆盖
        constexpr const char* kPlaceholderText = "-- Lua 脚本编辑器占位\n"
                                                 "-- 阶段 1：骨架 + Dock 集成已就绪\n"
                                                 "-- 阶段 2 将接入打开/保存，阶段 3 加 Lua 语法高亮\n"
                                                 "\n"
                                                 "function OnStart()\n"
                                                 "    print(\"Hello from in-editor script editor!\")\n"
                                                 "end\n";
    } // namespace

    ScriptEditorPanel::ScriptEditorPanel() : m_Editor(CreateScope<TextEditor>())
    {
        // 使用 ImGuiColorTextEdit 自带的 Lua 定义（阶段 3 会扩展关键字以覆盖 Engine API）
        m_Editor->SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
        m_Editor->SetText(kPlaceholderText);
        m_Editor->SetShowWhitespaces(false);
        m_Editor->SetTabSize(4);
    }

    // unique_ptr<TextEditor> 的析构点必须在 TextEditor 完整类型可见时
    ScriptEditorPanel::~ScriptEditorPanel() = default;

    void ScriptEditorPanel::OnImGuiRender()
    {
        if (!m_Visible)
            return;

        // 第一次出现时给一个合理的默认尺寸（dock 首次分配前生效）
        ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("脚本编辑器", &m_Visible))
        {
            ImGui::End();
            return;
        }

        // 推入代码等宽字体（JetBrains Mono + Sarasa Mono SC merge）
        // 若字体加载失败则 fallback 到 UI 默认字体（不 Push）
        ImFont* codeFont = ImGuiLayer::GetCodeFont();
        if (codeFont)
            ImGui::PushFont(codeFont);

        // 将编辑器填满整个面板剩余区域
        const ImVec2 editorSize = ImGui::GetContentRegionAvail();
        m_Editor->Render("##ScriptEditor", editorSize, /*aBorder=*/false);

        if (codeFont)
            ImGui::PopFont();

        ImGui::End();
    }

} // namespace Engine
