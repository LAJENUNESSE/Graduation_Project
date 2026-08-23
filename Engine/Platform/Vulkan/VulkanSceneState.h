#pragma once

#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace Engine
{

    class VulkanShader;

    // Phase 8.2 场景渲染状态机：模拟 OpenGL 即时模式的"当前 shader + 纹理槽"全局状态。
    // 上层调用序列（RenderQueue::Flush：Material 重放 → SetXxx → VAO Bind → DrawIndexed）
    // 依赖全局状态语义；Vulkan 无全局状态机，故在录制时刻由 DrawIndexed/DrawArrays
    // 消费此处快照组装 pipeline 与 descriptor set（SPEC D-A）。
    //
    // 写入端：
    //   - VulkanShader::Bind/Unbind                  → 当前 shader
    //   - VulkanTexture2D/Cubemap::Bind(slot)        → 纹理槽（OpenGL unit 号即槽号）
    //   - VulkanRendererAPI::Bind{Texture,Cubemap}View → 纹理槽（void* view/sampler 直通）
    class VulkanSceneState
    {
    public:
        static constexpr uint32_t kMaxTextureSlots = 16; // 对齐 PBR.glsl 用到的 unit 0~13 上限

        struct TextureBinding
        {
            VkImageView View    = VK_NULL_HANDLE;
            VkSampler   Sampler = VK_NULL_HANDLE;
            bool        Valid   = false;
        };

        void          SetCurrentShader(VulkanShader* shader) { m_CurrentShader = shader; }
        VulkanShader* GetCurrentShader() const { return m_CurrentShader; }

        void BindTextureSlot(uint32_t slot, VkImageView view, VkSampler sampler);
        void UnbindTextureSlot(uint32_t slot);

        // 帧首清空全部纹理槽（上一帧槽位对新一帧无意义，防止悬垂引用被复用）
        void ResetTextureSlots();

        // 未绑定/越界时返回 kInvalidBinding
        const TextureBinding& GetTextureSlot(uint32_t slot) const;

        static const TextureBinding kInvalidBinding;

    private:
        VulkanShader*                                m_CurrentShader = nullptr;
        std::array<TextureBinding, kMaxTextureSlots> m_TextureSlots{};
    };

} // namespace Engine
