#pragma once

#include "Core/Base.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <string>
#include <vector>
#include <mutex>

namespace Engine
{

    // ========== 日志条目 ==========
    struct ConsoleLogEntry
    {
        std::string Timestamp;
        spdlog::level::level_enum Level = spdlog::level::info;
        std::string Message;
    };

    // ========== 自定义 spdlog sink ==========
    class ImGuiConsoleSink : public spdlog::sinks::base_sink<std::mutex>
    {
    public:
        static constexpr size_t MaxEntries = 1000;

        const std::vector<ConsoleLogEntry>& GetEntries() const { return m_Entries; }
        void ClearEntries();

    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override;
        void flush_() override {}

    private:
        std::vector<ConsoleLogEntry> m_Entries;
    };

    // ========== 控制台面板 ==========
    class ConsolePanel
    {
    public:
        ConsolePanel();
        ~ConsolePanel();

        // 注册 sink 到 spdlog 日志器
        void RegisterSink();
        void UnregisterSink();

        void OnImGuiRender();

    private:
        Ref<ImGuiConsoleSink> m_Sink;
        bool m_SinkRegistered = false;

        // 过滤设置
        bool m_ShowTrace = true;
        bool m_ShowInfo = true;
        bool m_ShowWarn = true;
        bool m_ShowError = true;

        char m_SearchBuffer[256] = {};
        bool m_AutoScroll = true;
        bool m_ScrollToBottom = false;
    };

} // namespace Engine

