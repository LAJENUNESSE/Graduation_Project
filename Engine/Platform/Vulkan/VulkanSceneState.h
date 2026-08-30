#pragma once

#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace Engine
{

    class VulkanShader;
    class VertexArray;

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
        static constexpr uint32_t kMaxStorageSlots = 8;  // 粒子/草地 billboard 的 SSBO binding 0~7

        struct TextureBinding
        {
            VkImageView View    = VK_NULL_HANDLE;
            VkSampler   Sampler = VK_NULL_HANDLE;
            // image 期望 layout：scene dispatcher 写 descriptor 时按此 layout 设置
            // （shadow map / FBO depth 用 DEPTH_STENCIL_READ_ONLY_OPTIMAL，普通 texture
            // 用 SHADER_READ_ONLY_OPTIMAL）。BindTextureSlot 时由调用方声明。
            VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
            bool          Valid  = false;
        };

        struct StorageBinding
        {
            VkBuffer     Buffer = VK_NULL_HANDLE;
            VkDeviceSize Offset = 0;
            VkDeviceSize Range  = 0; // VK_WHOLE_SIZE 表示到 buffer 末尾
            bool         Valid  = false;
        };

        void          SetCurrentShader(VulkanShader* shader) { m_CurrentShader = shader; }
        VulkanShader* GetCurrentShader() const { return m_CurrentShader; }

        // 当前 VAO：DrawArrays 抽象层无 VAO 参数（天空盒等 Bind 后直接画），录制时刻取此快照
        void               SetCurrentVertexArray(const VertexArray* vao) { m_CurrentVertexArray = vao; }
        const VertexArray* GetCurrentVertexArray() const { return m_CurrentVertexArray; }

        void BindTextureSlot(uint32_t      slot,
                             VkImageView   view,
                             VkSampler     sampler,
                             VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED);
        void UnbindTextureSlot(uint32_t slot);

        // SSBO 槽：StorageBuffer::Bind(binding) 的 Vulkan 对应（OpenGL glBindBufferBase 语义）
        void BindStorageSlot(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);

        // 帧首清空全部纹理槽（上一帧槽位对新一帧无意义，防止悬垂引用被复用）
        void ResetTextureSlots();

        // 未绑定/越界时返回 kInvalidBinding
        const TextureBinding& GetTextureSlot(uint32_t slot) const;
        const StorageBinding& GetStorageSlot(uint32_t binding) const;

        static const TextureBinding kInvalidBinding;

    private:
        VulkanShader*                                m_CurrentShader      = nullptr;
        const VertexArray*                           m_CurrentVertexArray = nullptr;
        std::array<TextureBinding, kMaxTextureSlots> m_TextureSlots{};
        std::array<StorageBinding, kMaxStorageSlots> m_StorageSlots{};
    };

} // namespace Engine
