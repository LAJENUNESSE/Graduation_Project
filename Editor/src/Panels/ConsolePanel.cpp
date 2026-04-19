#include "Panels/ConsolePanel.h"
#include "Core/Log.h"

#include "ImGui/ImGuiLayer.h"

#include <imgui.h>
#include <spdlog/pattern_formatter.h>

#include <algorithm>
#include <cstring>

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

            ImVec4      backgroundColor(0.0f, 0.0f, 0.0f, 0.0f);
            ImVec4      accentColor(0.0f, 0.0f, 0.0f, 0.0f);
            ImVec4      color;
            const char* levelTag;
            switch (entry.Level)
            {
            case spdlog::level::trace:
            case spdlog::level::debug:
                color    = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                levelTag = "[TRACE]";
                break;
            case spdlog::level::info:
                color    = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                levelTag = "[INFO] ";
                break;
            case spdlog::level::warn:
                backgroundColor = ImVec4(0.50f, 0.38f, 0.05f, 0.34f);
                accentColor     = ImVec4(0.95f, 0.78f, 0.08f, 0.95f);
                color           = ImVec4(1.0f, 0.9f, 0.3f, 1.0f);
                levelTag        = "[WARN] ";
                break;
            case spdlog::level::err:
                backgroundColor = ImVec4(0.52f, 0.10f, 0.10f, 0.42f);
                accentColor     = ImVec4(1.0f, 0.28f, 0.28f, 0.98f);
                color           = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                levelTag        = "[ERROR]";
                break;
            case spdlog::level::critical:
                backgroundColor = ImVec4(0.68f, 0.05f, 0.05f, 0.52f);
                accentColor     = ImVec4(1.0f, 0.12f, 0.12f, 1.0f);
                color           = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                levelTag        = "[CRIT] ";
                break;
            default:
                color    = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                levelTag = "[????] ";
                break;
            }

            const std::string lineText = "[" + entry.Timestamp + "] " + levelTag + " " + entry.Message;
            if (backgroundColor.w > 0.0f)
            {
                ImDrawList*  drawList   = ImGui::GetWindowDrawList();
                const ImVec2 lineStart  = ImGui::GetCursorScreenPos();
                const float  rowHeight  = ImGui::GetTextLineHeightWithSpacing();
                const float  availWidth = ImGui::GetContentRegionAvail().x;
                const float  rectWidth  = std::max(ImGui::CalcTextSize(lineText.c_str()).x + 10.0f, availWidth);
                const ImVec2 rectMin(lineStart.x - 3.0f, lineStart.y);
                const ImVec2 rectMax(rectMin.x + rectWidth, lineStart.y + rowHeight);
                drawList->AddRectFilled(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(backgroundColor), 4.0f);

                if (accentColor.w > 0.0f)
                {
                    const ImVec2 accentMax(rectMin.x + 4.0f, rectMax.y);
                    drawList->AddRectFilled(rectMin, accentMax, ImGui::ColorConvertFloat4ToU32(accentColor), 2.0f);
                }
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(lineText.c_str());
            ImGui::PopStyleColor();
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
