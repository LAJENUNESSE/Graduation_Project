#include "Panels/ConsolePanel.h"
#include "Core/Log.h"

#include "ImGui/ImGuiLayer.h"

#include <imgui.h>
#include <spdlog/pattern_formatter.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string_view>

namespace Engine
{

    // ==================== ImGuiConsoleSink ====================

    std::vector<ConsoleLogEntry> ImGuiConsoleSink::CopyEntries()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return m_Entries;
    }
    void ImGuiConsoleSink::ClearEntries()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        m_Entries.clear();
        ++m_Version;
    }

    uint64_t ImGuiConsoleSink::GetVersion()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return m_Version;
    }

    void ImGuiConsoleSink::sink_it_(const spdlog::details::log_msg& msg)
    {
        // 格式化时间戳 HH:MM:SS
        auto    time = msg.time;
        auto    tt   = std::chrono::system_clock::to_time_t(time);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char timeBuf[16];
        std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

        ConsoleLogEntry entry;
        entry.Timestamp = timeBuf;
        entry.Level     = msg.level;
        entry.Message   = std::string(msg.payload.data(), msg.payload.size());

        m_Entries.push_back(std::move(entry));

        // 环形缓冲区：超过最大条数时删除最早的
        if (m_Entries.size() > MaxEntries)
            m_Entries.erase(m_Entries.begin());

        ++m_Version;
    }

    // ==================== ConsolePanel ====================

    ConsolePanel::ConsolePanel()
    {
        m_Sink = CreateRef<ImGuiConsoleSink>();
    }

    ConsolePanel::~ConsolePanel()
    {
        UnregisterSink();
    }

    void ConsolePanel::RegisterSink()
    {
        if (m_SinkRegistered)
            return;

        auto registerLoggerSink = [this](const Ref<spdlog::logger>& logger)
        {
            if (!logger)
                return;

            auto& sinks = logger->sinks();
            if (std::find(sinks.begin(), sinks.end(), m_Sink) == sinks.end())
                sinks.push_back(m_Sink);
        };

        registerLoggerSink(Log::GetCoreLogger());
        registerLoggerSink(Log::GetClientLogger());
        m_SinkRegistered = true;
    }

    void ConsolePanel::UnregisterSink()
    {
        if (!m_SinkRegistered)
            return;

        auto unregisterLoggerSink = [this](const Ref<spdlog::logger>& logger)
        {
            if (!logger)
                return;

            auto& sinks = logger->sinks();
            sinks.erase(std::remove(sinks.begin(), sinks.end(), m_Sink), sinks.end());
        };

        unregisterLoggerSink(Log::GetCoreLogger());
        unregisterLoggerSink(Log::GetClientLogger());
        m_SinkRegistered = false;
    }

    void ConsolePanel::OnImGuiRender()
    {
        ImGui::Begin("控制台"); // 控制台

        // 工具栏：级别过滤按钮
        {
            auto ToggleButton = [](const char* label, bool& value, const ImVec4& activeColor)
            {
                if (value)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(activeColor.x * 1.2f, activeColor.y * 1.2f,
                                                                         activeColor.z * 1.2f, 1.0f));
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
                }
                if (ImGui::Button(label))
                    value = !value;
                ImGui::PopStyleColor(2);
            };

            ToggleButton("Trace", m_ShowTrace, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::SameLine();
            ToggleButton("Info", m_ShowInfo, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            ImGui::SameLine();
            ToggleButton("Warn", m_ShowWarn, ImVec4(0.8f, 0.7f, 0.0f, 1.0f));
            ImGui::SameLine();
            ToggleButton("Error", m_ShowError, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));

            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();

            // 搜索框
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##搜索", "搜索...", m_SearchBuffer, sizeof(m_SearchBuffer)); // 搜索

            ImGui::SameLine();
            if (ImGui::Button("清空")) // 清空
                m_Sink->ClearEntries();

            ImGui::SameLine();
            ImGui::Checkbox("自动滚动", &m_AutoScroll); // 自动滚动
        }

        ImGui::Separator();

        // 日志内容区域
        ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        struct ConsoleTextSpan
        {
            std::string Text;
            ImVec4      Color;
        };

        auto IsAsciiSpace = [](char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; };
        auto IsAsciiAlpha = [](char ch) { return std::isalpha(static_cast<unsigned char>(ch)) != 0; };
        auto IsAsciiDigit = [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; };
        auto IsAsciiUpper = [](char ch) { return std::isupper(static_cast<unsigned char>(ch)) != 0; };
        auto ToLowerAscii = [&](std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            for (char ch : text)
                result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            return result;
        };
        auto IsTimeToken = [&](std::string_view token)
        {
            return token.size() == 8 && IsAsciiDigit(token[0]) && IsAsciiDigit(token[1]) && token[2] == ':' &&
                   IsAsciiDigit(token[3]) && IsAsciiDigit(token[4]) && token[5] == ':' && IsAsciiDigit(token[6]) &&
                   IsAsciiDigit(token[7]);
        };
        auto IsNumberToken = [&](std::string_view token)
        {
            if (token.empty())
                return false;

            size_t index = 0;
            if ((token[index] == '+' || token[index] == '-') && token.size() > 1)
                ++index;

            if (index + 2 <= token.size() && token[index] == '0' &&
                (token[index + 1] == 'x' || token[index + 1] == 'X'))
            {
                index += 2;
                if (index >= token.size())
                    return false;
                for (; index < token.size(); ++index)
                {
                    if (!std::isxdigit(static_cast<unsigned char>(token[index])))
                        return false;
                }
                return true;
            }

            bool hasDigit    = false;
            bool hasDot      = false;
            bool hasExponent = false;
            for (; index < token.size(); ++index)
            {
                const char ch = token[index];
                if (IsAsciiDigit(ch))
                {
                    hasDigit = true;
                    continue;
                }
                if (ch == '.' && !hasDot && !hasExponent)
                {
                    hasDot = true;
                    continue;
                }
                if ((ch == 'e' || ch == 'E') && hasDigit && !hasExponent)
                {
                    hasExponent = true;
                    hasDigit    = false;
                    if (index + 1 < token.size() && (token[index + 1] == '+' || token[index + 1] == '-'))
                        ++index;
                    continue;
                }
                if ((ch == 'f' || ch == 'F' || ch == 'u' || ch == 'U' || ch == 'l' || ch == 'L') && hasDigit &&
                    index + 1 == token.size())
                {
                    return true;
                }
                return false;
            }
            return hasDigit;
        };
        auto IsPathToken = [&](std::string_view token)
        {
            if (token.empty())
                return false;

            const std::string lower = ToLowerAscii(token);
            if (token.size() >= 3 && IsAsciiAlpha(token[0]) && token[1] == ':' && (token[2] == '\\' || token[2] == '/'))
                return true;
            if (lower.rfind("./", 0) == 0 || lower.rfind("../", 0) == 0)
                return true;
            if (token.find('\\') != std::string_view::npos || token.find('/') != std::string_view::npos)
                return true;

            static constexpr std::array<std::string_view, 15> kExtensions = {
                ".cpp",  ".h",    ".hpp", ".lua", ".scene", ".glsl", ".vert", ".frag",
                ".json", ".yaml", ".yml", ".obj", ".png",   ".csv",  ".exe",
            };
            for (std::string_view ext : kExtensions)
            {
                if (lower.size() > ext.size() && lower.find(ext) != std::string::npos)
                    return true;
            }
            return false;
        };
        auto IsBooleanToken = [](std::string_view lowerToken)
        {
            return lowerToken == "true" || lowerToken == "false" || lowerToken == "yes" || lowerToken == "no" ||
                   lowerToken == "on" || lowerToken == "off";
        };
        auto IsDiagnosticToken = [](std::string_view lowerToken)
        {
            static constexpr std::array<std::string_view, 12> kTokens = {
                "fail",      "failed", "error",     "warning", "warn",  "critical",
                "exception", "assert", "assertion", "crash",   "abort", "unknown",
            };
            return std::find(kTokens.begin(), kTokens.end(), lowerToken) != kTokens.end();
        };
        auto IsKeywordToken = [](std::string_view lowerToken)
        {
            static constexpr std::array<std::string_view, 24> kTokens = {
                "add",       "remove", "init",     "initialized", "shutdown", "creating", "created", "reload",
                "reloading", "open",   "opened",   "close",       "closed",   "entity",   "struct",  "class",
                "default",   "vendor", "renderer", "version",     "project",  "root",     "forcegl", "disablereadback",
            };
            return std::find(kTokens.begin(), kTokens.end(), lowerToken) != kTokens.end();
        };
        auto IsTypeToken = [&](std::string_view token)
        {
            if (token.find("::") != std::string_view::npos)
                return true;
            if (token.empty() || !IsAsciiUpper(token.front()))
                return false;

            bool hasInnerUpper = false;
            for (size_t i = 1; i < token.size(); ++i)
            {
                if (IsAsciiUpper(token[i]))
                {
                    hasInnerUpper = true;
                    break;
                }
            }
            if (hasInnerUpper)
                return true;

            static constexpr std::array<std::string_view, 10> kSuffixes = {
                "Component", "System", "Renderer", "Layer", "Panel", "Scene", "Entity", "Manager", "Engine", "Context",
            };
            for (std::string_view suffix : kSuffixes)
            {
                if (token.size() > suffix.size() && token.ends_with(suffix))
                    return true;
            }
            return false;
        };
        auto IsSplitPunctuation = [](char ch)
        {
            switch (ch)
            {
            case '=':
            case '(':
            case ')':
            case '{':
            case '}':
            case '<':
            case '>':
            case ',':
            case ';':
            case ':':
            case '+':
            case '-':
            case '*':
            case '!':
                return true;
            default:
                return false;
            }
        };
        auto GetLevelTextColor = [](spdlog::level::level_enum level)
        {
            switch (level)
            {
            case spdlog::level::trace:
            case spdlog::level::debug:
                return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
            case spdlog::level::info:
                return ImVec4(0.33f, 0.82f, 0.48f, 1.0f);
            case spdlog::level::warn:
                return ImVec4(1.0f, 0.86f, 0.28f, 1.0f);
            case spdlog::level::err:
                return ImVec4(1.0f, 0.36f, 0.36f, 1.0f);
            case spdlog::level::critical:
                return ImVec4(1.0f, 0.16f, 0.16f, 1.0f);
            default:
                return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            }
        };
        auto GetMessageDefaultColor = [](spdlog::level::level_enum level)
        {
            switch (level)
            {
            case spdlog::level::trace:
            case spdlog::level::debug:
                return ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
            case spdlog::level::warn:
                return ImVec4(0.88f, 0.86f, 0.78f, 1.0f);
            case spdlog::level::err:
            case spdlog::level::critical:
                return ImVec4(0.92f, 0.84f, 0.84f, 1.0f);
            case spdlog::level::info:
            default:
                return ImVec4(0.84f, 0.84f, 0.84f, 1.0f);
            }
        };
        auto GetLevelLabel = [](spdlog::level::level_enum level) -> std::string_view
        {
            switch (level)
            {
            case spdlog::level::trace:
            case spdlog::level::debug:
                return "TRACE";
            case spdlog::level::info:
                return "INFO";
            case spdlog::level::warn:
                return "WARN";
            case spdlog::level::err:
                return "ERROR";
            case spdlog::level::critical:
                return "CRIT";
            default:
                return "????";
            }
        };
        auto AppendSpan = [](std::vector<ConsoleTextSpan>& spans, std::string_view text, const ImVec4& color)
        {
            if (!text.empty())
                spans.push_back({std::string(text), color});
        };
        auto AppendStyledToken =
            [&](std::vector<ConsoleTextSpan>& spans, std::string_view token, const ImVec4& defaultColor)
        {
            if (token.empty())
                return;

            auto ClassifyChunk = [&](std::string_view chunk)
            {
                const std::string lower = ToLowerAscii(chunk);
                if (IsNumberToken(chunk))
                    return ImVec4(0.97f, 0.76f, 0.38f, 1.0f);
                if (IsPathToken(chunk))
                    return ImVec4(0.63f, 0.88f, 0.53f, 1.0f);
                if (IsBooleanToken(lower))
                    return ImVec4(0.82f, 0.58f, 1.0f, 1.0f);
                if (IsDiagnosticToken(lower))
                    return ImVec4(1.0f, 0.48f, 0.48f, 1.0f);
                if (IsKeywordToken(lower))
                    return ImVec4(0.86f, 0.58f, 1.0f, 1.0f);
                if (IsTypeToken(chunk))
                    return ImVec4(0.43f, 0.82f, 0.92f, 1.0f);
                return defaultColor;
            };

            const std::string lowerToken = ToLowerAscii(token);
            if (IsNumberToken(token) || IsPathToken(token) || IsBooleanToken(lowerToken) ||
                IsDiagnosticToken(lowerToken) || IsKeywordToken(lowerToken) || IsTypeToken(token))
            {
                AppendSpan(spans, token, ClassifyChunk(token));
                return;
            }

            size_t cursor = 0;
            while (cursor < token.size())
            {
                if (IsSplitPunctuation(token[cursor]))
                {
                    size_t opLength = 1;
                    if (cursor + 1 < token.size())
                    {
                        const char lhs = token[cursor];
                        const char rhs = token[cursor + 1];
                        if ((lhs == ':' && rhs == ':') || (lhs == '-' && rhs == '>') || (lhs == '=' && rhs == '>') ||
                            (lhs == '=' && rhs == '=') || (lhs == '!' && rhs == '=') || (lhs == '<' && rhs == '=') ||
                            (lhs == '>' && rhs == '='))
                        {
                            opLength = 2;
                        }
                    }

                    AppendSpan(spans, token.substr(cursor, opLength), ImVec4(0.92f, 0.56f, 0.30f, 1.0f));
                    cursor += opLength;
                    continue;
                }

                const size_t start = cursor;
                while (cursor < token.size() && !IsSplitPunctuation(token[cursor]))
                    ++cursor;
                const std::string_view chunk = token.substr(start, cursor - start);
                AppendSpan(spans, chunk, ClassifyChunk(chunk));
            }
        };
        auto BuildMessageSpans = [&](std::string_view message, spdlog::level::level_enum level)
        {
            std::vector<ConsoleTextSpan> spans;
            spans.reserve(message.size() / 2 + 4);

            const ImVec4 defaultColor = GetMessageDefaultColor(level);
            size_t       index        = 0;
            while (index < message.size())
            {
                const char ch = message[index];
                if (IsAsciiSpace(ch))
                {
                    const size_t start = index;
                    while (index < message.size() && IsAsciiSpace(message[index]))
                        ++index;
                    AppendSpan(spans, message.substr(start, index - start), defaultColor);
                    continue;
                }

                if (ch == '\'' || ch == '"')
                {
                    const char   quote = ch;
                    const size_t start = index++;
                    while (index < message.size())
                    {
                        if (message[index] == '\\' && index + 1 < message.size())
                        {
                            index += 2;
                            continue;
                        }
                        if (message[index] == quote)
                        {
                            ++index;
                            break;
                        }
                        ++index;
                    }
                    AppendSpan(spans, message.substr(start, index - start), ImVec4(0.74f, 0.92f, 0.50f, 1.0f));
                    continue;
                }

                if (ch == '[')
                {
                    const size_t end = message.find(']', index + 1);
                    if (end != std::string_view::npos)
                    {
                        AppendSpan(spans, message.substr(index, 1), ImVec4(0.52f, 0.52f, 0.52f, 1.0f));
                        const std::string_view tag = message.substr(index + 1, end - index - 1);
                        AppendSpan(spans, tag,
                                   IsTimeToken(tag) ? ImVec4(0.68f, 0.76f, 0.88f, 1.0f)
                                                    : ImVec4(0.42f, 0.78f, 0.98f, 1.0f));
                        AppendSpan(spans, message.substr(end, 1), ImVec4(0.52f, 0.52f, 0.52f, 1.0f));
                        index = end + 1;
                        continue;
                    }
                }

                const size_t start = index;
                while (index < message.size() && !IsAsciiSpace(message[index]) && message[index] != '[' &&
                       message[index] != '\'' && message[index] != '"')
                {
                    ++index;
                }
                AppendStyledToken(spans, message.substr(start, index - start), defaultColor);
            }

            return spans;
        };
        auto BuildLineSpans = [&](const ConsoleLogEntry& entry)
        {
            std::vector<ConsoleTextSpan> spans;
            spans.reserve(16);

            const ImVec4 bracketColor(0.52f, 0.52f, 0.52f, 1.0f);
            const ImVec4 timeColor(0.68f, 0.76f, 0.88f, 1.0f);
            const ImVec4 levelColor = GetLevelTextColor(entry.Level);

            AppendSpan(spans, "[", bracketColor);
            AppendSpan(spans, entry.Timestamp, timeColor);
            AppendSpan(spans, "] ", bracketColor);
            AppendSpan(spans, "[", bracketColor);
            AppendSpan(spans, GetLevelLabel(entry.Level), levelColor);
            AppendSpan(spans, "] ", bracketColor);

            std::vector<ConsoleTextSpan> messageSpans = BuildMessageSpans(entry.Message, entry.Level);
            spans.insert(spans.end(), messageSpans.begin(), messageSpans.end());
            return spans;
        };
        auto CalcSpanWidth = [](const std::vector<ConsoleTextSpan>& spans)
        {
            float width = 0.0f;
            for (const auto& span : spans)
                width += ImGui::CalcTextSize(span.Text.c_str(), span.Text.c_str() + span.Text.size()).x;
            return width;
        };
        auto DrawSpans = [](const std::vector<ConsoleTextSpan>& spans)
        {
            ImDrawList*  drawList = ImGui::GetWindowDrawList();
            const ImVec2 start    = ImGui::GetCursorScreenPos();
            float        x        = start.x;

            for (const auto& span : spans)
            {
                if (span.Text.empty())
                    continue;
                drawList->AddText(ImVec2(x, start.y), ImGui::ColorConvertFloat4ToU32(span.Color), span.Text.c_str(),
                                  span.Text.c_str() + span.Text.size());
                x += ImGui::CalcTextSize(span.Text.c_str(), span.Text.c_str() + span.Text.size()).x;
            }

            ImGui::Dummy(ImVec2(std::max(1.0f, x - start.x), ImGui::GetTextLineHeightWithSpacing()));
        };

        ImFont* codeFont = ImGuiLayer::GetCodeFont();
        if (codeFont)
            ImGui::PushFont(codeFont);

        // 仅在日志有变化时拷贝
        uint64_t currentVersion = m_Sink->GetVersion();
        if (currentVersion != m_CachedVersion)
        {
            m_CachedEntries = m_Sink->CopyEntries();
            m_CachedVersion = currentVersion;
        }

        const auto& entries = m_CachedEntries;
        std::string searchStr(m_SearchBuffer);

        for (const auto& entry : entries)
        {
            // 级别过滤
            bool show = false;
            switch (entry.Level)
            {
            case spdlog::level::trace:
                show = m_ShowTrace;
                break;
            case spdlog::level::debug:
                show = m_ShowTrace;
                break;
            case spdlog::level::info:
                show = m_ShowInfo;
                break;
            case spdlog::level::warn:
                show = m_ShowWarn;
                break;
            case spdlog::level::err:
                show = m_ShowError;
                break;
            case spdlog::level::critical:
                show = m_ShowError;
                break;
            default:
                break;
            }
            if (!show)
                continue;

            // 文本搜索过滤
            if (!searchStr.empty())
            {
                if (entry.Message.find(searchStr) == std::string::npos)
                    continue;
            }

            ImVec4 backgroundColor(0.0f, 0.0f, 0.0f, 0.0f);
            ImVec4 accentColor(0.0f, 0.0f, 0.0f, 0.0f);
            switch (entry.Level)
            {
            case spdlog::level::warn:
                backgroundColor = ImVec4(0.50f, 0.38f, 0.05f, 0.34f);
                accentColor     = ImVec4(0.95f, 0.78f, 0.08f, 0.95f);
                break;
            case spdlog::level::err:
                backgroundColor = ImVec4(0.52f, 0.10f, 0.10f, 0.42f);
                accentColor     = ImVec4(1.0f, 0.28f, 0.28f, 0.98f);
                break;
            case spdlog::level::critical:
                backgroundColor = ImVec4(0.68f, 0.05f, 0.05f, 0.52f);
                accentColor     = ImVec4(1.0f, 0.12f, 0.12f, 1.0f);
                break;
            default:
                break;
            }

            const std::vector<ConsoleTextSpan> lineSpans = BuildLineSpans(entry);
            if (backgroundColor.w > 0.0f)
            {
                ImDrawList*  drawList   = ImGui::GetWindowDrawList();
                const ImVec2 lineStart  = ImGui::GetCursorScreenPos();
                const float  rowHeight  = ImGui::GetTextLineHeightWithSpacing();
                const float  availWidth = ImGui::GetContentRegionAvail().x;
                const float  rectWidth  = std::max(CalcSpanWidth(lineSpans) + 10.0f, availWidth);
                const ImVec2 rectMin(lineStart.x - 3.0f, lineStart.y);
                const ImVec2 rectMax(rectMin.x + rectWidth, lineStart.y + rowHeight);
                drawList->AddRectFilled(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(backgroundColor), 4.0f);

                if (accentColor.w > 0.0f)
                {
                    const ImVec2 accentMax(rectMin.x + 4.0f, rectMax.y);
                    drawList->AddRectFilled(rectMin, accentMax, ImGui::ColorConvertFloat4ToU32(accentColor), 2.0f);
                }
            }

            DrawSpans(lineSpans);
        }

        if (codeFont)
            ImGui::PopFont();

        // 自动滚动到底部
        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
            m_ScrollToBottom = true;

        if (m_ScrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            m_ScrollToBottom = false;
        }

        ImGui::EndChild();

        ImGui::End();
    }

} // namespace Engine
