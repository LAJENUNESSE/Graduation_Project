#pragma once

#include <functional>

namespace Engine
{
    namespace CrashHandler
    {
        void Install();

        // 崩溃时紧急保存回调（best-effort，不保证成功）
        using EmergencySaveFn = std::function<void()>;
        void SetEmergencySaveCallback(EmergencySaveFn fn);
    } // namespace CrashHandler
} // namespace Engine
