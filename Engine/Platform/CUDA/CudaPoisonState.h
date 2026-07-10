// CUDA 全局中毒状态机 —— 从 CudaErrorHandling.h 提取的纯 C++ 部分。
// 不依赖 CUDA Runtime，可在普通 C++ 单元测试中使用。
#pragma once

#include <atomic>
#include <cstdio>

namespace Engine
{
    namespace CudaInterop
    {

        // ---- 通知回调（纯 C 函数指针，nvcc 安全）----

        using CudaPoisonNotifyFn = void (*)(const char* reason);

        inline CudaPoisonNotifyFn& PoisonNotifyCallback()
        {
            static CudaPoisonNotifyFn s_Callback = nullptr;
            return s_Callback;
        }

        inline void SetCudaPoisonNotify(CudaPoisonNotifyFn fn)
        {
            PoisonNotifyCallback() = fn;
        }

        // ---- 全局中毒标记 ----

        inline std::atomic<bool>& PoisonFlag()
        {
            static std::atomic<bool> s_Poisoned{false};
            return s_Poisoned;
        }

        inline const char*& PoisonReasonStorage()
        {
            static const char* s_Reason = nullptr;
            return s_Reason;
        }

        inline bool IsCudaPoisoned()
        {
            return PoisonFlag().load(std::memory_order_relaxed);
        }

        inline const char* GetCudaPoisonReason()
        {
            return PoisonReasonStorage() ? PoisonReasonStorage() : "unknown";
        }

        inline void PoisonCuda(const char* reason)
        {
            bool expected = false;
            if (PoisonFlag().compare_exchange_strong(expected, true))
            {
                PoisonReasonStorage() = reason;
                fprintf(stderr, "[CUDA] Context poisoned, all subsequent CUDA calls will be skipped. Reason: %s\n",
                        reason);
                if (PoisonNotifyCallback())
                    PoisonNotifyCallback()(reason);
            }
        }

    } // namespace CudaInterop
} // namespace Engine
