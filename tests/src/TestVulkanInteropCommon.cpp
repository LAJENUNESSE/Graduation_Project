#include <gtest/gtest.h>
#include "CUDA/VulkanInteropCommon.h"

using namespace Engine::CudaInterop;

TEST(VulkanInteropCommon, SyncValueFrameZero)
{
    const auto v = BuildInteropFrameSyncValues(0);
    EXPECT_EQ(v.CudaSignalValue, 1u);
    EXPECT_EQ(v.VulkanSignalValue, 2u);
    EXPECT_EQ(v.WaitVulkanValue, 0u);
}

TEST(VulkanInteropCommon, SyncValueFrameN)
{
    const auto v = BuildInteropFrameSyncValues(7);
    EXPECT_EQ(v.CudaSignalValue, 15u);
    EXPECT_EQ(v.VulkanSignalValue, 16u);
    EXPECT_EQ(v.WaitVulkanValue, 14u);
}

TEST(VulkanInteropCommon, HandleTypeDefaultMatchesPlatform)
{
#ifdef _WIN32
    EXPECT_EQ(GetDefaultExternalHandleType(), ExternalHandleType::OpaqueWin32);
#else
    EXPECT_EQ(GetDefaultExternalHandleType(), ExternalHandleType::OpaqueFd);
#endif
}

TEST(VulkanInteropCommon, HandleTypeMappingHelpers)
{
    EXPECT_STREQ(ToString(ExternalHandleType::OpaqueWin32), "OpaqueWin32");
    EXPECT_STREQ(ToString(ExternalHandleType::OpaqueFd), "OpaqueFd");

    EXPECT_TRUE(IsKnownExternalHandleType(ExternalHandleType::OpaqueWin32));
    EXPECT_TRUE(IsKnownExternalHandleType(ExternalHandleType::OpaqueFd));
    EXPECT_FALSE(IsKnownExternalHandleType(static_cast<ExternalHandleType>(255)));
}

TEST(VulkanInteropCommon, ClosePolicyByHandleType)
{
    EXPECT_TRUE(ShouldCloseHandleAfterCudaImport(ExternalHandleType::OpaqueWin32));
    EXPECT_TRUE(ShouldCloseHandleAfterCudaImport(ExternalHandleType::OpaqueFd));
    EXPECT_FALSE(ShouldCloseHandleAfterCudaImport(static_cast<ExternalHandleType>(255)));
}
TEST(VulkanInteropCommon, HandleOwnershipTransferredAfterImport)
{
    OwnedInteropHandle handle{};
#ifdef _WIN32
    handle.Value = reinterpret_cast<NativeInteropHandle>(static_cast<uintptr_t>(0x1234));
#else
    handle.Value = 7;
#endif

    ASSERT_TRUE(handle.IsValid());
    ASSERT_TRUE(ShouldCloseHandleAfterCudaImport(GetDefaultExternalHandleType()));

    handle.MarkImportedByCuda();
    EXPECT_TRUE(handle.ImportedByCuda);
    EXPECT_FALSE(handle.IsValid());
}

