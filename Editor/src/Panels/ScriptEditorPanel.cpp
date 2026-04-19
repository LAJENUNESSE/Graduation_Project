#include "ScriptEditorPanel.h"

#include "Asset/PathUtils.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "ImGui/ImGuiLayer.h"

#include <TextEditor.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <imgui.h>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace
{

    enum class CompletionContext : uint8_t
    {
        Global = 0,
        SelfMember,
        EngineMember,
        EntityMember,
        KeyMember,
        MouseMember,
    };

    enum class CompletionItemKind : uint8_t
    {
        Keyword,
        Function,
        Method,
        Constant,
        Table,
    };

    struct CompletionItem
    {
        std::string        Label;
        std::string        InsertText;
        std::string        Detail;
        CompletionItemKind Kind     = CompletionItemKind::Keyword;
        int                Priority = 0;
    };

    struct CompletionQuery
    {
        CompletionContext Context = CompletionContext::Global;
        std::string       Prefix;
        int               ReplaceLine        = 0;
        int               ReplaceColumnStart = 0;
        int               ReplaceColumnEnd   = 0;
    };

    bool IsIdentifierChar(char ch)
    {
        const unsigned char c = static_cast<unsigned char>(ch);
        return std::isalnum(c) != 0 || ch == '_';
    }

    bool IsReceiverChar(char ch)
    {
        return IsIdentifierChar(ch) || ch == '.';
    }

    std::string ToLowerAscii(std::string_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (char ch : text)
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        return result;
    }

    bool StartsWith(std::string_view text, std::string_view prefix)
    {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    bool StartsWithInsensitive(std::string_view text, std::string_view prefix)
    {
        if (text.size() < prefix.size())
            return false;
        return ToLowerAscii(text.substr(0, prefix.size())) == ToLowerAscii(prefix);
    }

    bool ContainsInsensitive(std::string_view text, std::string_view needle)
    {
        return ToLowerAscii(text).find(ToLowerAscii(needle)) != std::string::npos;
    }

    bool IsLuaTableSymbol(std::string_view name)
    {
        static constexpr std::array<std::string_view, 11> kTableNames = {
            "_G", "coroutine", "table", "io", "os", "string", "utf8", "bit32", "math", "debug", "package",
        };
        return std::find(kTableNames.begin(), kTableNames.end(), name) != kTableNames.end();
    }

    bool IsLuaValueSymbol(std::string_view name)
    {
        static constexpr std::array<std::string_view, 9> kValueNames = {
            "_VERSION", "pi", "huge", "maxinteger", "mininteger", "preload", "cpath", "path", "loaded",
        };
        return IsLuaTableSymbol(name) || std::find(kValueNames.begin(), kValueNames.end(), name) != kValueNames.end();
    }

    const char* GetCompletionKindLabel(CompletionItemKind kind)
    {
        switch (kind)
        {
        case CompletionItemKind::Keyword:
            return "Keyword";
        case CompletionItemKind::Function:
            return "Function";
        case CompletionItemKind::Method:
            return "Method";
        case CompletionItemKind::Constant:
            return "Constant";
        case CompletionItemKind::Table:
            return "Table";
        }
        return "Item";
    }

    std::vector<CompletionItem> BuildGlobalCompletionItems()
    {
        std::vector<CompletionItem>     items;
        std::unordered_set<std::string> seen;
        const auto&                     lua = TextEditor::LanguageDefinition::Lua();

        auto add = [&](CompletionItem item)
        {
            if (item.Label.empty())
                return;
            if (!seen.insert(item.Label).second)
                return;
            items.push_back(std::move(item));
        };

        add({"Engine", "Engine", "Engine API table", CompletionItemKind::Table, 100});
        add({"Key", "Key", "Keyboard constants", CompletionItemKind::Table, 95});
        add({"Mouse", "Mouse", "Mouse constants", CompletionItemKind::Table, 95});
        add({"self", "self", "Current script instance", CompletionItemKind::Table, 90});
        add({"script", "script", "Script table", CompletionItemKind::Table, 85});

        for (const auto& keyword : lua.mKeywords)
            add({keyword, keyword, "Lua keyword", CompletionItemKind::Keyword, 70});

        for (const auto& [name, identifier] : lua.mIdentifiers)
        {
            if (IsLuaTableSymbol(name))
                add({name, name, "Lua built-in table", CompletionItemKind::Table, 55});
            else if (IsLuaValueSymbol(name))
                add({name, name, "Lua built-in value", CompletionItemKind::Constant, 55});
            else
                add({name, name + std::string("()"), identifier.mDeclaration, CompletionItemKind::Function, 50});
        }

        return items;
    }

    const std::vector<CompletionItem>& GetGlobalCompletionItems()
    {
        static const std::vector<CompletionItem> kItems = BuildGlobalCompletionItems();
        return kItems;
    }

    const std::vector<CompletionItem>& GetEngineCompletionItems()
    {
        static const std::vector<CompletionItem> kItems = {
            {"Info", "Info()", "Engine logging", CompletionItemKind::Function, 90},
            {"Warn", "Warn()", "Engine logging", CompletionItemKind::Function, 90},
            {"Error", "Error()", "Engine logging", CompletionItemKind::Function, 90},
            {"Debug", "Debug()", "Engine logging", CompletionItemKind::Function, 90},
            {"IsKeyPressed", "IsKeyPressed()", "Input query", CompletionItemKind::Function, 85},
            {"IsMouseButtonPressed", "IsMouseButtonPressed()", "Input query", CompletionItemKind::Function, 85},
            {"GetMousePosition", "GetMousePosition()", "Input query", CompletionItemKind::Function, 80},
        };
        return kItems;
    }

    const std::vector<CompletionItem>& GetSelfCompletionItems()
    {
        static const std::vector<CompletionItem> kItems = {
            {"Entity", "Entity", "Bound entity handle", CompletionItemKind::Table, 95},
        };
        return kItems;
    }

    const std::vector<CompletionItem>& GetEntityCompletionItems()
    {
        static const std::vector<CompletionItem> kItems = {
            {"GetTranslation", "GetTranslation()", "Entity method", CompletionItemKind::Method, 95},
            {"SetTranslation", "SetTranslation()", "Entity method", CompletionItemKind::Method, 95},
            {"GetWorldTranslation", "GetWorldTranslation()", "Entity method", CompletionItemKind::Method, 90},
            {"GetRotation", "GetRotation()", "Entity method", CompletionItemKind::Method, 90},
            {"SetRotation", "SetRotation()", "Entity method", CompletionItemKind::Method, 90},
            {"GetWorldRotation", "GetWorldRotation()", "Entity method", CompletionItemKind::Method, 85},
            {"GetScale", "GetScale()", "Entity method", CompletionItemKind::Method, 85},
            {"SetScale", "SetScale()", "Entity method", CompletionItemKind::Method, 85},
            {"GetForward", "GetForward()", "Entity method", CompletionItemKind::Method, 85},
            {"Translate", "Translate()", "Entity method", CompletionItemKind::Method, 85},
            {"Rotate", "Rotate()", "Entity method", CompletionItemKind::Method, 85},
            {"GetName", "GetName()", "Entity method", CompletionItemKind::Method, 80},
            {"DestroySelf", "DestroySelf()", "Entity method", CompletionItemKind::Method, 80},
            {"DistanceTo", "DistanceTo()", "Entity method", CompletionItemKind::Method, 75},
        };
        return kItems;
    }

    const std::vector<CompletionItem>& GetKeyCompletionItems()
    {
        static const std::vector<CompletionItem> kItems = {
            {"KEY_W", "KEY_W", "Keyboard constant", CompletionItemKind::Constant, 90},
            {"KEY_A", "KEY_A", "Keyboard constant", CompletionItemKind::Constant, 90},
            {"KEY_S", "KEY_S", "Keyboard constant", CompletionItemKind::Constant, 90},
            {"KEY_D", "KEY_D", "Keyboard constant", CompletionItemKind::Constant, 90},
            {"KEY_Q", "KEY_Q", "Keyboard constant", CompletionItemKind::Constant, 90},
            {"KEY_E", "KEY_E", "Keyboard constant", CompletionItemKind::Constant, 90},
            {"KEY_SPACE", "KEY_SPACE", "Keyboard constant", CompletionItemKind::Constant, 85},
            {"KEY_ESCAPE", "KEY_ESCAPE", "Keyboard constant", CompletionItemKind::Constant, 85},
            {"KEY_UP", "KEY_UP", "Keyboard constant", CompletionItemKind::Constant, 80},
            {"KEY_DOWN", "KEY_DOWN", "Keyboard constant", CompletionItemKind::Constant, 80},
            {"KEY_LEFT", "KEY_LEFT", "Keyboard constant", CompletionItemKind::Constant, 80},
            {"KEY_RIGHT", "KEY_RIGHT", "Keyboard constant", CompletionItemKind::Constant, 80},
            {"KEY_SHIFT", "KEY_SHIFT", "Keyboard constant", CompletionItemKind::Constant, 80},
            {"KEY_CTRL", "KEY_CTRL", "Keyboard constant", CompletionItemKind::Constant, 80},
            {"KEY_F1", "KEY_F1", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F2", "KEY_F2", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F3", "KEY_F3", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F4", "KEY_F4", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F5", "KEY_F5", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F6", "KEY_F6", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F7", "KEY_F7", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F8", "KEY_F8", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F9", "KEY_F9", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F10", "KEY_F10", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F11", "KEY_F11", "Keyboard constant", CompletionItemKind::Constant, 70},
            {"KEY_F12", "KEY_F12", "Keyboard constant", CompletionItemKind::Constant, 70},
        };
        return kItems;
    }

    const std::vector<CompletionItem>& GetMouseCompletionItems()
    {
        static const std::vector<CompletionItem> kItems = {
            {"MOUSE_LEFT", "MOUSE_LEFT", "Mouse constant", CompletionItemKind::Constant, 90},
            {"MOUSE_RIGHT", "MOUSE_RIGHT", "Mouse constant", CompletionItemKind::Constant, 90},
            {"MOUSE_MIDDLE", "MOUSE_MIDDLE", "Mouse constant", CompletionItemKind::Constant, 85},
        };
        return kItems;
    }

    const std::vector<CompletionItem>& GetCompletionItems(CompletionContext context)
    {
        switch (context)
        {
        case CompletionContext::SelfMember:
            return GetSelfCompletionItems();
        case CompletionContext::EngineMember:
            return GetEngineCompletionItems();
        case CompletionContext::EntityMember:
            return GetEntityCompletionItems();
        case CompletionContext::KeyMember:
            return GetKeyCompletionItems();
        case CompletionContext::MouseMember:
            return GetMouseCompletionItems();
        case CompletionContext::Global:
        default:
            return GetGlobalCompletionItems();
        }
    }

    int ScoreCompletionItem(const CompletionItem& item, std::string_view prefix)
    {
        if (prefix.empty())
            return item.Priority * 100;
        if (StartsWith(item.Label, prefix))
            return 4000 + item.Priority * 100 - static_cast<int>(item.Label.size());
        if (StartsWithInsensitive(item.Label, prefix))
            return 3000 + item.Priority * 100 - static_cast<int>(item.Label.size());
        if (ContainsInsensitive(item.Label, prefix))
            return 2000 + item.Priority * 100 - static_cast<int>(item.Label.size());
        return std::numeric_limits<int>::min();
    }

    std::vector<int> BuildCandidateIndices(CompletionContext context, std::string_view prefix)
    {
        struct Match
        {
            int Index = -1;
            int Score = 0;
        };

        const auto&        items = GetCompletionItems(context);
        std::vector<Match> matches;
        matches.reserve(items.size());

        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            const int score = ScoreCompletionItem(items[i], prefix);
            if (score == std::numeric_limits<int>::min())
                continue;
            matches.push_back({i, score});
        }

        std::sort(matches.begin(), matches.end(),
                  [&](const Match& lhs, const Match& rhs)
                  {
                      if (lhs.Score != rhs.Score)
                          return lhs.Score > rhs.Score;
                      return items[lhs.Index].Label < items[rhs.Index].Label;
                  });

        std::vector<int> indices;
        indices.reserve(std::min<size_t>(matches.size(), 64));
        for (const Match& match : matches)
        {
            indices.push_back(match.Index);
            if (indices.size() >= 64)
                break;
        }
        return indices;
    }

    bool BuildCompletionQuery(TextEditor& editor, bool manualTrigger, CompletionQuery& outQuery)
    {
        const TextEditor::Coordinates cursor(editor.GetCursorPosition());
        const std::string             beforeCursor = editor.GetText(TextEditor::Coordinates(cursor.mLine, 0), cursor);

        size_t prefixStart = beforeCursor.size();
        while (prefixStart > 0 && IsIdentifierChar(beforeCursor[prefixStart - 1]))
            --prefixStart;

        CompletionContext context        = CompletionContext::Global;
        size_t            receiverCursor = prefixStart;
        while (receiverCursor > 0 && std::isspace(static_cast<unsigned char>(beforeCursor[receiverCursor - 1])) != 0)
            --receiverCursor;

        if (receiverCursor > 0)
        {
            const char op = beforeCursor[receiverCursor - 1];
            if (op == '.' || op == ':')
            {
                size_t receiverEnd = receiverCursor - 1;
                while (receiverEnd > 0 && std::isspace(static_cast<unsigned char>(beforeCursor[receiverEnd - 1])) != 0)
                    --receiverEnd;

                size_t receiverStart = receiverEnd;
                while (receiverStart > 0 && IsReceiverChar(beforeCursor[receiverStart - 1]))
                    --receiverStart;

                const std::string_view receiver(beforeCursor.data() + receiverStart, receiverEnd - receiverStart);
                if (op == '.' && receiver == "self")
                    context = CompletionContext::SelfMember;
                else if (op == '.' && receiver == "Engine")
                    context = CompletionContext::EngineMember;
                else if (op == ':' && receiver == "self.Entity")
                    context = CompletionContext::EntityMember;
                else if (op == '.' && receiver == "Key")
                    context = CompletionContext::KeyMember;
                else if (op == '.' && receiver == "Mouse")
                    context = CompletionContext::MouseMember;
                else if (!manualTrigger)
                    return false;
            }
        }

        const std::string prefix = beforeCursor.substr(prefixStart);
        if (!manualTrigger && context == CompletionContext::Global && prefix.empty())
            return false;

        outQuery.Context            = context;
        outQuery.Prefix             = prefix;
        outQuery.ReplaceLine        = cursor.mLine;
        outQuery.ReplaceColumnStart = std::max(0, cursor.mColumn - static_cast<int>(prefix.size()));
        outQuery.ReplaceColumnEnd   = cursor.mColumn;
        return true;
    }

    bool IsTriggerCodepoint(uint32_t codepoint)
    {
        if (codepoint == '.' || codepoint == ':')
            return true;
        if (codepoint > 127)
            return false;
        return IsIdentifierChar(static_cast<char>(codepoint));
    }

} // namespace

namespace Engine
{

    ScriptEditorPanel::ScriptEditorPanel()  = default;
    ScriptEditorPanel::~ScriptEditorPanel() = default;

    ScriptEditorPanel::Document* ScriptEditorPanel::FindDocumentByStableId(uint32_t stableId)
    {
        for (Document& doc : m_Documents)
        {
            if (doc.StableId == stableId)
                return &doc;
        }
        return nullptr;
    }

    void ScriptEditorPanel::BindDocumentCallbacks(Document& doc)
    {
        if (!doc.Editor)
            return;

        const uint32_t stableId = doc.StableId;
        doc.Editor->SetCharTypedCallback([this, stableId](ImWchar codepoint)
                                         { HandleEditorChar(stableId, static_cast<uint32_t>(codepoint)); });
        doc.Editor->SetKeyPressedCallback(
            [this, stableId](ImGuiKey key, bool ctrl, bool shift, bool alt)
            { return HandleEditorKey(stableId, static_cast<int>(key), ctrl, shift, alt); });
    }

    void ScriptEditorPanel::HandleEditorChar(uint32_t stableId, uint32_t codepoint)
    {
        Document* doc = FindDocumentByStableId(stableId);
        if (!doc)
            return;
        doc->Completion.PendingTypedChars.push_back(codepoint);
    }

    bool ScriptEditorPanel::HandleEditorKey(uint32_t stableId, int key, bool ctrl, bool shift, bool alt)
    {
        Document* doc = FindDocumentByStableId(stableId);
        if (!doc)
            return false;

        CompletionSession& completion = doc->Completion;
        const ImGuiKey     keyCode    = static_cast<ImGuiKey>(key);

        if (ctrl && !shift && !alt && keyCode == ImGuiKey_Space)
        {
            completion.RequestManualTrigger = true;
            return true;
        }

        if (!completion.Visible || completion.CandidateIndices.empty())
            return false;

        switch (keyCode)
        {
        case ImGuiKey_UpArrow:
            completion.SelectedIndex = completion.SelectedIndex <= 0
                                           ? static_cast<int>(completion.CandidateIndices.size()) - 1
                                           : completion.SelectedIndex - 1;
            return true;
        case ImGuiKey_DownArrow:
            completion.SelectedIndex =
                (completion.SelectedIndex + 1) % static_cast<int>(completion.CandidateIndices.size());
            return true;
        case ImGuiKey_Enter:
        case ImGuiKey_Tab:
            completion.RequestAccept = true;
            return true;
        case ImGuiKey_Escape:
            completion.RequestClose = true;
            return true;
        default:
            return false;
        }
    }

    void ScriptEditorPanel::TriggerCompletion(Document& doc, bool manualTrigger)
    {
        if (!doc.Editor)
        {
            CloseCompletion(doc);
            return;
        }

        CompletionQuery query;
        if (!BuildCompletionQuery(*doc.Editor, manualTrigger, query))
        {
            CloseCompletion(doc);
            return;
        }

        CompletionSession& completion = doc.Completion;
        completion.CandidateIndices   = BuildCandidateIndices(query.Context, query.Prefix);
        if (completion.CandidateIndices.empty())
        {
            CloseCompletion(doc);
            return;
        }

        completion.Visible            = true;
        completion.SelectedIndex      = 0;
        completion.LastScrollIndex    = -1;
        completion.ContextId          = static_cast<uint8_t>(query.Context);
        completion.ReplaceLine        = query.ReplaceLine;
        completion.ReplaceColumnStart = query.ReplaceColumnStart;
        completion.ReplaceColumnEnd   = query.ReplaceColumnEnd;
        completion.Prefix             = query.Prefix;
    }

    void ScriptEditorPanel::AcceptCompletion(Document& doc)
    {
        CompletionSession& completion = doc.Completion;
        if (!doc.Editor || !completion.Visible || completion.CandidateIndices.empty())
        {
            CloseCompletion(doc);
            return;
        }

        if (completion.SelectedIndex < 0 ||
            completion.SelectedIndex >= static_cast<int>(completion.CandidateIndices.size()))
        {
            CloseCompletion(doc);
            return;
        }

        const CompletionContext            context   = static_cast<CompletionContext>(completion.ContextId);
        const std::vector<CompletionItem>& items     = GetCompletionItems(context);
        const int                          itemIndex = completion.CandidateIndices[completion.SelectedIndex];
        if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size()))
        {
            CloseCompletion(doc);
            return;
        }

        const TextEditor::Coordinates replaceStart(completion.ReplaceLine, completion.ReplaceColumnStart);
        const TextEditor::Coordinates replaceEnd(completion.ReplaceLine, completion.ReplaceColumnEnd);
        doc.Editor->SetSelection(replaceStart, replaceEnd);
        doc.Editor->SetCursorPosition(replaceEnd);
        if (replaceStart != replaceEnd)
            doc.Editor->Delete();
        doc.Editor->InsertText(items[itemIndex].InsertText.c_str());
        doc.Dirty = true;
        CloseCompletion(doc);
    }

    void ScriptEditorPanel::CloseCompletion(Document& doc)
    {
        CompletionSession& completion = doc.Completion;
        completion.Visible            = false;
        completion.SelectedIndex      = 0;
        completion.LastScrollIndex    = -1;
        completion.ContextId          = static_cast<uint8_t>(CompletionContext::Global);
        completion.ReplaceLine        = 0;
        completion.ReplaceColumnStart = 0;
        completion.ReplaceColumnEnd   = 0;
        completion.Prefix.clear();
        completion.CandidateIndices.clear();
        completion.PendingTypedChars.clear();
        completion.RequestManualTrigger = false;
        completion.RequestAccept        = false;
        completion.RequestClose         = false;
    }

    void ScriptEditorPanel::ProcessCompletion(Document& doc)
    {
        CompletionSession& completion = doc.Completion;

        if (completion.RequestClose)
        {
            CloseCompletion(doc);
            return;
        }

        if (completion.RequestAccept)
        {
            completion.RequestAccept = false;
            AcceptCompletion(doc);
            return;
        }

        const bool manualTrigger        = completion.RequestManualTrigger;
        completion.RequestManualTrigger = false;

        std::vector<uint32_t> typedChars;
        typedChars.swap(completion.PendingTypedChars);

        if (manualTrigger)
        {
            TriggerCompletion(doc, true);
            return;
        }

        if (!typedChars.empty())
        {
            bool shouldTrigger = false;
            bool shouldClose   = false;
            for (uint32_t codepoint : typedChars)
            {
                if (IsTriggerCodepoint(codepoint))
                    shouldTrigger = true;
                else
                    shouldClose = true;
            }

            if (shouldTrigger)
            {
                TriggerCompletion(doc, false);
                return;
            }

            if (shouldClose && completion.Visible)
            {
                CloseCompletion(doc);
                return;
            }
        }

        if (doc.Editor && completion.Visible && doc.Editor->IsTextChanged())
        {
            TriggerCompletion(doc, false);
            return;
        }

        if (doc.Editor && completion.Visible && doc.Editor->IsCursorPositionChanged())
            CloseCompletion(doc);
    }

    void ScriptEditorPanel::RenderCompletionPopup(Document& doc)
    {
        CompletionSession& completion = doc.Completion;
        if (!completion.Visible || !doc.Editor || completion.CandidateIndices.empty())
            return;

        const CompletionContext            context = static_cast<CompletionContext>(completion.ContextId);
        const std::vector<CompletionItem>& items   = GetCompletionItems(context);
        if (completion.SelectedIndex < 0 ||
            completion.SelectedIndex >= static_cast<int>(completion.CandidateIndices.size()))
            completion.SelectedIndex = 0;

        const int   candidateCount = static_cast<int>(completion.CandidateIndices.size());
        const float rowHeight      = ImGui::GetTextLineHeightWithSpacing();
        const float visibleRows    = static_cast<float>(std::min(candidateCount, 8));
        ImVec2      popupSize(520.0f, visibleRows * rowHeight + 16.0f);
        ImVec2      popupPos = doc.Editor->GetCursorScreenPosition();
        popupPos.y += std::max(doc.Editor->GetLineHeight(), rowHeight);

        if (ImGuiViewport* viewport = ImGui::GetMainViewport())
        {
            const float rightEdge  = viewport->WorkPos.x + viewport->WorkSize.x;
            const float bottomEdge = viewport->WorkPos.y + viewport->WorkSize.y;
            if (popupPos.x + popupSize.x > rightEdge)
                popupPos.x = std::max(viewport->WorkPos.x, rightEdge - popupSize.x);
            if (popupPos.y + popupSize.y > bottomEdge)
                popupPos.y = std::max(viewport->WorkPos.y, doc.Editor->GetCursorScreenPosition().y - popupSize.y);
        }

        const std::string windowName = "##ScriptCompletionPopup_" + std::to_string(doc.StableId);
        ImGui::SetNextWindowPos(popupPos);
        ImGui::SetNextWindowSize(popupSize);

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_AlwaysVerticalScrollbar;

        if (!ImGui::Begin(windowName.c_str(), nullptr, flags))
        {
            ImGui::End();
            return;
        }

        for (int i = 0; i < candidateCount; ++i)
        {
            const int itemIndex = completion.CandidateIndices[i];
            if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size()))
                continue;

            const CompletionItem& item     = items[itemIndex];
            const bool            selected = i == completion.SelectedIndex;

            ImGui::PushID(i);
            if (ImGui::Selectable(item.Label.c_str(), selected, 0, ImVec2(240.0f, 0.0f)))
            {
                completion.SelectedIndex = i;
                completion.RequestAccept = true;
            }
            if (ImGui::IsItemHovered())
                completion.SelectedIndex = i;
            ImGui::SameLine(252.0f);
            ImGui::TextDisabled("%s", GetCompletionKindLabel(item.Kind));
            ImGui::SameLine(332.0f);
            ImGui::TextDisabled("%s", item.Detail.c_str());
            ImGui::PopID();

            if (selected && completion.LastScrollIndex != i)
            {
                ImGui::SetScrollHereY(0.25f);
                completion.LastScrollIndex = i;
            }
        }

        if (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            !doc.Editor->IsFocused())
            CloseCompletion(doc);

        ImGui::End();
    }

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
        doc.StableId    = m_NextDocId++;
        doc.Editor->SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
        doc.Editor->SetText(content);
        doc.Editor->SetShowWhitespaces(false);
        doc.Editor->SetTabSize(4);
        BindDocumentCallbacks(doc);
        doc.Dirty      = false;
        doc.WindowOpen = true;

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

                ProcessCompletion(doc);
                RenderCompletionPopup(doc);

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
