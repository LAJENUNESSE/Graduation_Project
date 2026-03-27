// TestVulkanInteropDiagnostics.cpp
// 测试 VulkanInteropCapabilityReport 结果映射和 VkExtSkeleton 诊断结构

#include <gtest/gtest.h>
#include "Core/VulkanExternalRuntime.h"

using namespace Engine;

// ============================================================================
// VkExtCapabilityFailReason 测试
// ============================================================================

TEST(VulkanInteropDiagnostics, FailReasonToStringMapping)
{
    // 确保所有失败原因都有对应的字符串表示
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::None), "None");
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::VulkanNotAvailable), "VulkanNotAvailable");
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::NoSuitablePhysicalDevice), "NoSuitablePhysicalDevice");
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::TimelineSemaphoreNotSupported), "TimelineSemaphoreNotSupported");
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::ExternalMemoryNotSupported), "ExternalMemoryNotSupported");
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::ExternalSemaphoreNotSupported), "ExternalSemaphoreNotSupported");
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::DeviceMismatch), "DeviceMismatch");
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::CudaNotAvailable), "CudaNotAvailable");
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::ResourceCreationFailed), "ResourceCreationFailed");
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::SelfCheckFailed), "SelfCheckFailed");
    EXPECT_STREQ(ToString(VkExtCapabilityFailReason::Unknown), "Unknown");
}

TEST(VulkanInteropDiagnostics, FailReasonEnumValues)
{
    // 确保枚举值稳定（用于序列化/日志）
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::None), 0);
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::VulkanNotAvailable), 1);
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::NoSuitablePhysicalDevice), 2);
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::TimelineSemaphoreNotSupported), 3);
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::ExternalMemoryNotSupported), 4);
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::ExternalSemaphoreNotSupported), 5);
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::DeviceMismatch), 6);
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::CudaNotAvailable), 7);
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::ResourceCreationFailed), 8);
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::SelfCheckFailed), 9);
    EXPECT_EQ(static_cast<uint8_t>(VkExtCapabilityFailReason::Unknown), 10);
}

// ============================================================================
// VulkanInteropCapabilityReport 测试
// ============================================================================

TEST(VulkanInteropDiagnostics, CapabilityReportDefaultValues)
{
    VulkanInteropCapabilityReport report{};
    EXPECT_FALSE(report.Supported);
    EXPECT_EQ(report.FailReason, VkExtCapabilityFailReason::None);
    EXPECT_EQ(report.VulkanApiVersion, 0u);
    EXPECT_EQ(report.VulkanDeviceId, 0u);
    EXPECT_EQ(report.CudaDeviceIndex, -1);
    EXPECT_EQ(report.DeviceName[0], '\0');
}

TEST(VulkanInteropDiagnostics, CapabilityReportSuccessCase)
{
    VulkanInteropCapabilityReport report{};
    report.Supported        = true;
    report.FailReason       = VkExtCapabilityFailReason::None;
    report.VulkanApiVersion = (1 << 22) | (3 << 12) | 275; // Vulkan 1.3.275
    report.VulkanDeviceId   = 0x2684;                      // RTX 4090
    report.CudaDeviceIndex  = 0;
    strncpy(report.DeviceName, "NVIDIA GeForce RTX 4090", sizeof(report.DeviceName) - 1);

    EXPECT_TRUE(report.Supported);
    EXPECT_EQ(report.FailReason, VkExtCapabilityFailReason::None);
    EXPECT_GT(report.VulkanApiVersion, 0u);
    EXPECT_GT(report.VulkanDeviceId, 0u);
    EXPECT_GE(report.CudaDeviceIndex, 0);
    EXPECT_STRNE(report.DeviceName, "");
}

TEST(VulkanInteropDiagnostics, CapabilityReportFailureCase)
{
    VulkanInteropCapabilityReport report{};
    report.Supported  = false;
    report.FailReason = VkExtCapabilityFailReason::TimelineSemaphoreNotSupported;

    EXPECT_FALSE(report.Supported);
    EXPECT_EQ(report.FailReason, VkExtCapabilityFailReason::TimelineSemaphoreNotSupported);
    EXPECT_STREQ(ToString(report.FailReason), "TimelineSemaphoreNotSupported");
}

// ============================================================================
// VkExtSkeletonStatus 测试
// ============================================================================

TEST(VulkanInteropDiagnostics, SkeletonStatusEnumValues)
{
    EXPECT_EQ(static_cast<uint8_t>(VkExtSkeletonStatus::NotRun), 0);
    EXPECT_EQ(static_cast<uint8_t>(VkExtSkeletonStatus::Success), 1);
    EXPECT_EQ(static_cast<uint8_t>(VkExtSkeletonStatus::Failed), 2);
}

// ============================================================================
// VkExtSkeletonDiagnostics 测试
// ============================================================================

TEST(VulkanInteropDiagnostics, SkeletonDiagnosticsDefaultValues)
{
    VkExtSkeletonDiagnostics diag{};
    EXPECT_FALSE(diag.Active);
    EXPECT_EQ(diag.LastStatus, VkExtSkeletonStatus::NotRun);
    EXPECT_FLOAT_EQ(diag.CudaWaitMs, 0.0f);
    EXPECT_FLOAT_EQ(diag.CudaKernelMs, 0.0f);
    EXPECT_FLOAT_EQ(diag.VkSubmitMs, 0.0f);
}

TEST(VulkanInteropDiagnostics, SkeletonDiagnosticsActiveState)
{
    VkExtSkeletonDiagnostics diag{};
    diag.Active       = true;
    diag.LastStatus   = VkExtSkeletonStatus::Success;
    diag.CudaWaitMs   = 0.05f;
    diag.CudaKernelMs = 0.12f;
    diag.VkSubmitMs   = 0.03f;

    EXPECT_TRUE(diag.Active);
    EXPECT_EQ(diag.LastStatus, VkExtSkeletonStatus::Success);
    EXPECT_GT(diag.CudaWaitMs, 0.0f);
    EXPECT_GT(diag.CudaKernelMs, 0.0f);
    EXPECT_GT(diag.VkSubmitMs, 0.0f);
}
