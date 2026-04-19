#include "ScriptEditorPanel.h"

#include "Asset/PathUtils.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "ImGui/ImGuiLayer.h"

#include <TextEditor.h>
#include <fstream>
#include <imgui.h>
#include <sstream>

namespace Engine
{

    ScriptEditorPanel::ScriptEditorPanel()  = default;
    ScriptEditorPanel::~ScriptEditorPanel() = default;

    // ────────────────────────────────────────────────────────────────
    // 文件 IO / 文档管理
    // ────────────────────────────────────────────────────────────────

    int ScriptEditorPanel::FindDocumentByPath(const std::filesystem::path& path) const
    {
        if (path.empty())
            return -1;

        std::error_code       ec;
        std::filesystem::path canon = std::filesystem::weakly_canonical(path, ec);
        if (ec)
            canon = path;

        for (size_t i = 0; i < m_Documents.size(); ++i)
        {
            const auto& docPath = m_Documents[i].Path;
            if (docPath.empty())
                continue;

            std::filesystem::path other = std::filesystem::weakly_canonical(docPath, ec);
            if (ec)
                other = docPath;

            if (other == canon)
                return static_cast<int>(i);
        }
        return -1;
    }

    bool ScriptEditorPanel::LoadFileContent(const std::filesystem::path& path, std::string& outContent) const
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            ENGINE_WARN("无法打开脚本文件: {0}", path.string());
            return false;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        outContent = ss.str();
        return true;
    }

    bool ScriptEditorPanel::WriteFileContent(const std::filesystem::path& path, const std::string& content) const
    {
        std::ofstream f(path, std::ios::binary);
        if (!f)
        {
            ENGINE_ERROR("无法写入脚本文件: {0}", path.string());
            return false;
        }
        f << content;
        return true;
    }

    void ScriptEditorPanel::EmplaceDocument(const std::filesystem::path& path, const std::string& content)
    {
        Document doc;
        doc.Path        = path;
        doc.DisplayName = path.empty() ? std::string("Untitled") : path.filename().string();
        doc.Editor      = CreateScope<TextEditor>();
        doc.Editor->SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
        doc.Editor->SetText(content);
        doc.Editor->SetShowWhitespaces(false);
        doc.Editor->SetTabSize(4);
        doc.Dirty      = false;
        doc.WindowOpen = true;
        doc.StableId   = m_NextDocId++;

        m_Documents.push_back(std::move(doc));
        m_ActiveIndex = static_cast<int>(m_Documents.size()) - 1;
    }

    void ScriptEditorPanel::OpenFile(const std::string& pathStr)
    {
        if (pathStr.empty())
            return;

        const std::filesystem::path path = PathUtils::ResolvePath(pathStr);

        // 已打开 → 激活
        const int existing = FindDocumentByPath(path);
        if (existing >= 0)
        {
            m_ActiveIndex = existing;
            m_Visible     = true;
            return;
        }

        std::string content;
        if (!LoadFileContent(path, content))
            return;

        EmplaceDocument(path, content);
        m_Visible = true;
        ENGINE_INFO("打开脚本: {0}", path.string());
    }

    bool ScriptEditorPanel::SaveDocument(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_Documents.size()))
            return false;

        Document&             doc      = m_Documents[index];
        std::filesystem::path savePath = doc.Path;

        // 未命名文档 → 弹另存为
        if (savePath.empty())
        {
            const std::string picked = FileDialogs::SaveFile("*.lua", "Lua 脚本");
            if (picked.empty())
                return false;
            savePath = picked;
        }

        const std::string content = doc.Editor->GetText();
        if (!WriteFileContent(savePath, content))
            return false;

        doc.Path        = savePath;
        doc.DisplayName = savePath.filename().string();
        doc.Dirty       = false;
        ENGINE_INFO("已保存脚本: {0}", savePath.string());
        return true;
    }

    bool ScriptEditorPanel::TryCloseDocument(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_Documents.size()))
            return false;

        Document& doc = m_Documents[index];
        if (doc.Dirty)
        {
            const std::string msg = "文件 \"" + doc.DisplayName +
                                    "\" 有未保存的修改。\n\n是：保存并关闭\n否：丢弃修改并关闭\n取消：放弃关闭";
            const auto result = FileDialogs::ShowYesNoCancelBox("未保存的修改", msg.c_str());

            if (result == FileDialogs::MessageBoxResult::Cancel)
                return false;
            if (result == FileDialogs::MessageBoxResult::Yes)
            {
                if (!SaveDocument(index))
                    return false; // 用户在另存为对话框里取消 → 保持不关闭
            }
            // No → 丢弃直接关闭
        }

        m_Documents.erase(m_Documents.begin() + index);

        // 修正 ActiveIndex
        if (m_ActiveIndex >= static_cast<int>(m_Documents.size()))
            m_ActiveIndex = static_cast<int>(m_Documents.size()) - 1;
        return true;
    }

    // ────────────────────────────────────────────────────────────────
    // UI
    // ────────────────────────────────────────────────────────────────

    void ScriptEditorPanel::RenderToolbar()
    {
        if (ImGui::Button("打开..."))
        {
            const std::string picked = FileDialogs::OpenFile("*.lua", "Lua 脚本");
            if (!picked.empty())
                OpenFile(picked);
        }

        const bool hasActive = m_ActiveIndex >= 0 && m_ActiveIndex < static_cast<int>(m_Documents.size());

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasActive);

        if (ImGui::Button("保存"))
            SaveDocument(m_ActiveIndex);

        ImGui::SameLine();
        if (ImGui::Button("另存为..."))
        {
            Document&         doc    = m_Documents[m_ActiveIndex];
            const std::string picked = FileDialogs::SaveFile("*.lua", "Lua 脚本");
            if (!picked.empty())
            {
                const std::filesystem::path savePath = picked;
                const std::string           content  = doc.Editor->GetText();
                if (WriteFileContent(savePath, content))
                {
                    doc.Path        = savePath;
                    doc.DisplayName = savePath.filename().string();
                    doc.Dirty       = false;
                    ENGINE_INFO("已另存为: {0}", savePath.string());
                }
            }
        }

        ImGui::EndDisabled();

        // 当前路径 / 未命名指示
        if (hasActive)
        {
            ImGui::SameLine();
            const auto&       doc      = m_Documents[m_ActiveIndex];
            const std::string pathText = doc.Path.empty() ? std::string("(未命名)") : doc.Path.string();
            ImGui::TextDisabled("|  %s%s", pathText.c_str(), doc.Dirty ? " *" : "");
        }
    }

    void ScriptEditorPanel::RenderTabBar()
    {
        if (m_Documents.empty())
        {
            ImGui::TextDisabled("尚未打开脚本。双击资产浏览器中的 .lua 文件，或点击上方 \"打开...\" 按钮。");
            return;
        }

        const ImGuiTabBarFlags flags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll;
        if (!ImGui::BeginTabBar("##ScriptDocs", flags))
            return;

        ImFont*   codeFont          = ImGuiLayer::GetCodeFont();
        int       requestCloseIndex = -1;
        const int docCount          = static_cast<int>(m_Documents.size());

        for (int i = 0; i < docCount; ++i)
        {
            Document& doc = m_Documents[i];

            // tab label：DisplayName + dirty 标记 + 稳定 ID（防止重名冲突）
            std::string label = doc.DisplayName;
            if (doc.Dirty)
                label += " *";
            label += "###scriptdoc_" + std::to_string(doc.StableId);

            doc.WindowOpen = true;
            if (ImGui::BeginTabItem(label.c_str(), &doc.WindowOpen))
            {
                m_ActiveIndex = i;

                if (codeFont)
                    ImGui::PushFont(codeFont);

                const ImVec2 editorSize = ImGui::GetContentRegionAvail();
                doc.Editor->Render("##editor", editorSize, /*aBorder=*/false);

                if (codeFont)
                    ImGui::PopFont();

                // 检测文本变更（IsTextChanged 返回"自上次调用以来是否变更"，要累积到 doc.Dirty）
                if (doc.Editor->IsTextChanged())
                    doc.Dirty = true;

                ImGui::EndTabItem();
            }

            // 记录第一个请求关闭的 tab（循环中不能立刻 erase）
            if (!doc.WindowOpen && requestCloseIndex < 0)
                requestCloseIndex = i;
        }

        ImGui::EndTabBar();

        // 延迟关闭（可能弹 Modal + erase）
        if (requestCloseIndex >= 0)
            TryCloseDocument(requestCloseIndex);
    }

    void ScriptEditorPanel::OnImGuiRender()
    {
        if (!m_Visible)
            return;

        ImGui::SetNextWindowSize(ImVec2(800.0f, 520.0f), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("脚本编辑器", &m_Visible))
        {
            ImGui::End();
            return;
        }

        RenderToolbar();
        ImGui::Separator();
        RenderTabBar();

        ImGui::End();
    }

} // namespace Engine
