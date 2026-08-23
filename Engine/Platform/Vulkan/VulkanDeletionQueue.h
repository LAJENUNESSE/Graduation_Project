#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <functional>
#include <vector>

namespace Engine
{

    // 按帧槽轮转的 GPU 资源延迟删除队列。
    //
    // 背景：编辑器整个 OnImGuiRender（含 OpenScene 等面板回调）处于主帧命令缓冲
    // 的录制窗口内（BeginFrame ~ EndFrame），此时同步 vkDestroy* 会把正在录制
    // 的命令缓冲打成 invalid state——vkDeviceWaitIdle 只能等待已提交的工作，
    // 对尚未 submit 的命令缓冲无效。因此录制窗口内产生的销毁必须推迟：
    //
    //   入队槽位 = 当前帧 m_CurrentFrame；
    //   执行时机 = 下一次该槽成为当前帧且 vkWaitForFences 通过之后
    //             （即至少两帧后，引用该对象的命令缓冲必然已完成执行）。
    //
    // kSlotCount 必须与 VulkanContext::MAX_FRAMES_IN_FLIGHT 保持一致。
    class VulkanDeletionQueue
    {
    public:
        static constexpr uint32_t kSlotCount = 2;

        // slot 范围 [0, kSlotCount)；fn 为空时忽略
        void Submit(uint32_t slot, std::function<void(VkDevice)>&& fn);

        // 执行并清空指定槽位的全部回调
        void FlushSlot(uint32_t slot, VkDevice device);
        void FlushAll(VkDevice device);

    private:
        std::array<std::vector<std::function<void(VkDevice)>>, kSlotCount> m_Slots;
    };

} // namespace Engine
