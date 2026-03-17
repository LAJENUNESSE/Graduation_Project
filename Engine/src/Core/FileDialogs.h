#pragma once

#include <string>

namespace Engine
{

    class FileDialogs
    {
    public:
        // filter: e.g. "*.scene" or "*.png;*.jpg;*.bmp"
        // description: e.g. "场景文件" (shown in dialog)
        // Returns empty string if user cancelled
        static std::string OpenFile(const char* filter, const char* description = nullptr);
        static std::string SaveFile(const char* filter, const char* description = nullptr);

        // 三选一消息对话框
        enum class MessageBoxResult
        {
            Cancel = 0,
            Yes    = 1,
            No     = 2
        };
        static MessageBoxResult ShowYesNoCancelBox(const char* title, const char* message);
    };

} // namespace Engine
