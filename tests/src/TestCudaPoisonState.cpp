#include <gtest/gtest.h>
#include "CUDA/CudaPoisonState.h"

using namespace Engine::CudaInterop;

class CudaPoisonStateTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 每个测试前重置全局状态
        PoisonFlag().store(false, std::memory_order_relaxed);
        PoisonReasonStorage() = nullptr;
        SetCudaPoisonNotify(nullptr);
    }
};

TEST_F(CudaPoisonStateTest, InitiallyNotPoisoned)
{
    EXPECT_FALSE(IsCudaPoisoned());
}

TEST_F(CudaPoisonStateTest, PoisonSetsFlag)
{
    PoisonCuda("test error");
    EXPECT_TRUE(IsCudaPoisoned());
}

TEST_F(CudaPoisonStateTest, PoisonReasonStored)
{
    PoisonCuda("out of memory");
    EXPECT_STREQ(GetCudaPoisonReason(), "out of memory");
}

TEST_F(CudaPoisonStateTest, DoublePoisonIgnored)
{
    PoisonCuda("first error");
    PoisonCuda("second error");
    // 第一次的 reason 应保留
    EXPECT_STREQ(GetCudaPoisonReason(), "first error");
}

TEST_F(CudaPoisonStateTest, NotifyCallbackFired)
{
    static int         callCount = 0;
    static const char* lastReason = nullptr;
    callCount   = 0;
    lastReason  = nullptr;

    SetCudaPoisonNotify([](const char* reason) {
        callCount++;
        lastReason = reason;
    });

    PoisonCuda("callback test");
    EXPECT_EQ(callCount, 1);
    EXPECT_STREQ(lastReason, "callback test");
}

TEST_F(CudaPoisonStateTest, NotifyOnlyOnFirstPoison)
{
    static int callCount = 0;
    callCount = 0;

    SetCudaPoisonNotify([](const char* reason) {
        callCount++;
    });

    PoisonCuda("first");
    PoisonCuda("second");
    EXPECT_EQ(callCount, 1);
}
