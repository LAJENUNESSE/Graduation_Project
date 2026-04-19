#pragma once

#include "Core/Base.h"

// 前向声明：TextEditor.h 自身 include <imgui.h>，不在 header 里暴露避免污染下游 TU
class TextEditor;

namespace Engine
{

    // 内置 Lua 脚本编辑器面板（基于 ImGuiColorTextEdit）
    //
    // 当前阶段（1/6）：骨架 + Dock 集成
    //   - 在 Dock 主区域和视口同组显示（tab 切换）
    //   - 使用 ImGuiLayer::GetCodeFont() 作为等宽代码字体
    //   - 视图菜单可切换可见性
    //   - 内容为占位文本
    //
    // 后续阶段将补齐：文件打开/保存 / Lua 语法高亮 / Engine API 补全 /
    // 运行时错误行高亮 / 主题与润色
    class ScriptEditorPanel
    {
    public:
        ScriptEditorPanel();
        ~ScriptEditorPanel();

        // ImGui 渲染入口，每帧由 EditorPanelCoordinator::RenderPanels() 调用
        void OnImGuiRender();

        // 可见性控制（视图菜单 / 键盘快捷键）
        bool IsVisible() const { return m_Visible; }
        void SetVisible(bool visible) { m_Visible = visible; }
        void ToggleVisible() { m_Visible = !m_Visible; }

    private:
        // unique_ptr 持有不完整类型：析构函数必须在 .cpp 中定义（TextEditor.h 被 include 后）
        Scope<TextEditor> m_Editor;
        bool              m_Visible = true;
    };

} // namespace Engine
