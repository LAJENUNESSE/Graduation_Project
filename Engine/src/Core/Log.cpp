#include "engpch.h"
#include "Core/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Engine
{

    Ref<spdlog::logger> Log::s_CoreLogger;
    Ref<spdlog::logger> Log::s_ClientLogger;

    void Log::Init()
    {
#ifdef _WIN32
        // 项目源/执行字符集都是 UTF-8 (/utf-8)，spdlog 输出的 byte 序列也是 UTF-8。
        // Windows 控制台默认按当前 ANSI 代码页(中文 Windows = GBK)解码 → 中文乱码。
        // 强制控制台输入/输出码页 = UTF-8 (65001) 以正确显示中文。
        ::SetConsoleOutputCP(CP_UTF8);
        ::SetConsoleCP(CP_UTF8);
#endif

        spdlog::set_pattern("%^[%T] %n: %v%$");

        s_CoreLogger = spdlog::stdout_color_mt("ENGINE");
        s_CoreLogger->set_level(spdlog::level::trace);

        s_ClientLogger = spdlog::stdout_color_mt("APP");
        s_ClientLogger->set_level(spdlog::level::trace);
    }

} // namespace Engine
