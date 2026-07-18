#include <gtest/gtest.h>

#include "Platform/CUDA/CudaPoisonState.h"

#include <string>

using namespace Engine::CudaInterop;

namespace
{
    // RAII guard — 中毒状态是单例 sticky flag，每个用例前后必须重置以隔离全局状态。
    // 生产代码禁止调用 Internal_ResetPoisonForTests()。
    struct PoisonResetGuard
    {
        PoisonResetGuard() { Internal_ResetPoisonForTests(); }
        ~PoisonResetGuard() { Internal_ResetPoisonForTests(); }
    };

    // 捕获通知回调的静态 sink——避免 lambda 隐式状态丢失。
    struct NotifySink
    {
        int        callCount = 0;
        std::string lastReason;

        void reset()
        {
            callCount  = 0;
            lastReason.clear();
        }
    };

    NotifySink& Sink()
    {
        static NotifySink s;
        return s;
    }

    void ResetSink()
    {
        Sink().reset();
    }
} // namespace

TEST(CudaPoisonState, InitialStateIsNotPoisoned)
{
    PoisonResetGuard guard;
    EXPECT_FALSE(IsCudaPoisoned());
    EXPECT_STREQ(GetCudaPoisonReason(), "unknown");
}

TEST(CudaPoisonState, PoisonOnceSetsFlagAndReason)
{
    PoisonResetGuard guard;
    PoisonCuda("first error");
    EXPECT_TRUE(IsCudaPoisoned());
    EXPECT_STREQ(GetCudaPoisonReason(), "first error");
}

TEST(CudaPoisonState, PoisonTwiceDoesNotOverwriteReason)
{
    PoisonResetGuard guard;
    // CAS 守不变量：compare_exchange_strong 仅在 false→true 时进入 reason 写入分支，
    // 二次 PoisonCuda 会因 expected=false 失败而不执行 reason 赋值。
    PoisonCuda("first error");
    PoisonCuda("second error");
    EXPECT_TRUE(IsCudaPoisoned());
    EXPECT_STREQ(GetCudaPoisonReason(), "first error");
}

TEST(CudaPoisonState, NotifyCallbackFiresOncePerPoison)
{
    PoisonResetGuard guard;
    ResetSink();
    SetCudaPoisonNotify([](const char* reason)
                         { Sink().callCount++; Sink().lastReason = reason; });

    PoisonCuda("trigger A");
    EXPECT_EQ(Sink().callCount, 1);
    EXPECT_EQ(Sink().lastReason, "trigger A");

    // 二次中毒不触发回调——CAS 已落到分支外
    PoisonCuda("trigger B");
    EXPECT_EQ(Sink().callCount, 1);
    EXPECT_EQ(Sink().lastReason, "trigger A");

    // 清空回调以避免污染后续用例
    SetCudaPoisonNotify(nullptr);
}

TEST(CudaPoisonState, NullReasonFallsBackToUnknown)
{
    PoisonResetGuard guard;
    // 默认状态（未 PoisonCuda 过）GetCudaPoisonReason 应返回 "unknown"
    EXPECT_STREQ(GetCudaPoisonReason(), "unknown");
}

TEST(CudaPoisonState, ResetRestoresUnpoisonedState)
{
    PoisonResetGuard guard;
    PoisonCuda("temporary");
    ASSERT_TRUE(IsCudaPoisoned());

    Internal_ResetPoisonForTests();
    EXPECT_FALSE(IsCudaPoisoned());
    EXPECT_STREQ(GetCudaPoisonReason(), "unknown");
}