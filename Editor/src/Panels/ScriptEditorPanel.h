#pragma once

#include "Core/Base.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// 前向声明：TextEditor.h 自身 include <imgui.h>，不在 header 里暴露避免污染下游 TU
class TextEditor;

namespace Engine
{

    // 内置 Lua 脚本编辑器面板（基于 ImGuiColorTextEdit）
    //
    // 阶段 2：多文档打开/保存
    //   - vector<Document> + ImGui TabBar 管理多个打开的脚本文件
    //   - 双击 AssetBrowser 中的 .lua 触发 OpenFile（通过 EditorPanelCoordinator::OpenScript）
    //   - 工具栏：打开 / 保存 / 另存为
    //   - Dirty 标记 + 关闭 tab 时弹 FileDialogs::ShowYesNoCancelBox 三选（保存/丢弃/取消）
    class ScriptEditorPanel
    {
    public:
        ScriptEditorPanel();
        ~ScriptEditorPanel();

        void OnImGuiRender();

        // 打开一个文件（新建 tab；若已打开则激活该 tab）
        // path 可以是项目相对路径或绝对路径
        void OpenFile(const std::string& path);

        // 可见性控制（视图菜单 / 外部调用）
        bool IsVisible() const { return m_Visible; }
        void SetVisible(bool visible) { m_Visible = visible; }
        void ToggleVisible() { m_Visible = !m_Visible; }

    private:
        struct Document
        {
            std::filesystem::path Path;        // 空 = 未命名（从未保存过）
            std::string           DisplayName; // tab 上显示的名字
            Scope<TextEditor>     Editor;
            bool                  Dirty      = false;
            bool                  WindowOpen = true; // ImGui TabItem p_open，点 x 变 false
            uint32_t              StableId   = 0;    // 稳定 ID，用于 ImGui TabItem 无歧义标识
        };

        int  FindDocumentByPath(const std::filesystem::path& path) const;
        bool LoadFileContent(const std::filesystem::path& path, std::string& outContent) const;
        bool WriteFileContent(const std::filesystem::path& path, const std::string& content) const;
        void EmplaceDocument(const std::filesystem::path& path, const std::string& content);

        // 保存指定索引的 Document；Path 为空则弹另存为对话框
        // 返回 true 表示保存成功；false 表示 IO 失败或用户取消了另存为
        bool SaveDocument(int index);

        // 关闭指定索引的 Document；dirty 时弹 Modal 三选
        // 返回 true 表示真的关闭了（m_Documents 已 erase）
        bool TryCloseDocument(int index);

        void RenderToolbar();
        void RenderTabBar();

    private:
        std::vector<Document> m_Documents;
        int                   m_ActiveIndex = -1;
        uint32_t              m_NextDocId   = 1; // 单调递增，给每个 doc 一个永久 ID
        bool                  m_Visible     = true;
    };

} // namespace Engine
