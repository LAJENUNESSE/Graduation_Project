#pragma once

#include "Core/Base.h"
#include "Renderer/GPUAsyncReadback.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace Engine
{

    // 3 槽 ring buffer 异步回读。
    //
    // 设计要点：
    // - 每槽独立 VkFence（不复用 swapchain inFlightFence），主帧 submit 后由
    //   VulkanContext::EndFrame 追加零 cmd submit 信号化。
    // - 每槽 host-visible + HOST_COHERENT + persistent mapped staging buffer。
    // - 内部 round-robin（m_WriteSlot / m_ReadSlot），不耦合 swapchain frame index。
    // - 接口语义与 GPUAsyncReadback 保持兼容：CopyFrom 占新槽、IsReady/GetData
    //   消费最老槽；ring 满时旧槽未消费会被覆盖（调用方应保证消费节奏）。
    //
    // 见 SPEC §3 D-10 / §4 P-9。
    class VulkanAsyncReadback : public GPUAsyncReadback
    {
    public:
        explicit VulkanAsyncReadback(uint32_t size);
        ~VulkanAsyncReadback() override;

        void CopyFrom(const Ref<ShaderStorageBuffer>& src, uint32_t size, uint32_t srcOffset = 0) override;
        bool IsReady() const override;
        void GetData(void* dest, uint32_t size) override;
        void Reset() override;
        bool IsPending() const override;

    private:
        struct Slot
        {
            VkBuffer      Staging   = VK_NULL_HANDLE;
            VmaAllocation Alloc     = nullptr;
            void*         Mapped    = nullptr;
            VkFence       Fence     = VK_NULL_HANDLE; // 创建为 unsignaled，每次 CopyFrom 在 EndFrame signal
            bool          Pending   = false;
            uint32_t      BytesUsed = 0;
        };

        std::array<Slot, 3> m_Slots;
        uint32_t            m_Size      = 0;
        uint32_t            m_WriteSlot = 0;
        uint32_t            m_ReadSlot  = 0;
    };

} // namespace Engine
