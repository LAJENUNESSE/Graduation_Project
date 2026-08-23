#pragma once

#include "Renderer/VertexArray.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace Engine
{

    class VulkanShader;

    // Phase 8.2 场景绘制分发器：DrawIndexed / DrawArrays 的真实 Vulkan 录制路径。
    //
    // 每次绘制：
    //   1. 从当前 shader 的 CPU uniform 缓存打包 std140 全局/光照/材质 UBO 并写入
    //      host-visible buffer（与 PBR.glsl Vulkan 分支布局逐字节对齐）
    //   2. 写 push constant（per-draw 变换）
    //   3. 从 per-frame descriptor pool 分配 set0/set1 并按反射 binding 写入
    //      （sampler 经场景状态机槽表解析：GLSL uniform 名 → OpenGL unit 号）
    //   4. 经 VulkanGraphicsPipelineBuilder 查/建 pipeline 后绑定 + 绘制
    class VulkanSceneDrawDispatcher
    {
    public:
        static constexpr uint32_t kMaxFramesInFlight = 2;   // 与 VulkanContext::MAX_FRAMES_IN_FLIGHT 一致
        static constexpr uint32_t kGlobalUboSize     = 640; // PBRGlobalUBO std140 实际数据
        static constexpr uint32_t kLightsUboSize     = 800; // PBRLightsUBO std140
        static constexpr uint32_t kMaterialUboSize   = 64;  // PBRMaterialUBO std140 实际数据

        // VkDescriptorBufferInfo.offset 对 UNIFORM_BUFFER 必须对齐
        // minUniformBufferOffsetAlignment（spec 最小保证 256）——违规在部分驱动上
        // 直接 device lost。所有 UBO 段偏移 / ring 步进一律按 256 对齐。
        static constexpr uint32_t kUboOffsetAlignment        = 256;
        static constexpr uint32_t kGlobalUboAligned          = 768; // 640 向上取整到 256 倍数
        static constexpr uint32_t kLightsUboOffset           = kGlobalUboAligned;
        static constexpr uint32_t kMaterialRingStride        = 256; // 数据 64B，步进对齐到 256
        static constexpr uint32_t kMaxMaterialAllocsPerFrame = 2048;

        void Init(VkDevice device);
        void Shutdown(VkDevice device);

        // 帧首调用：重置材质 ring 写指针 + descriptor pool
        void OnBeginFrame(uint32_t frameIndex);

        struct DrawParams
        {
            VkCommandBuffer Cmd                  = VK_NULL_HANDLE;
            VkRenderPass    RenderPass           = VK_NULL_HANDLE; // 当前激活的场景 renderpass
            uint32_t        ColorAttachmentCount = 1;
            uint32_t        ViewportWidth        = 0;
            uint32_t        ViewportHeight       = 0;

            bool     Indexed      = false;
            uint32_t IndexCount   = 0;
            uint32_t VertexCount  = 0; // non-indexed 用
            uint32_t FirstIndex   = 0;
            int32_t  VertexOffset = 0;
            uint32_t FirstVertex  = 0; // non-indexed 用

            // 光栅状态（来自 VulkanRendererAPI 成员缓存）
            bool DepthTest   = true;
            bool DepthWrite  = true;
            bool DepthLEqual = false;
            bool CullBack    = true;
        };

        // 返回 false 表示资源未就绪（无 shader / 无 renderpass / descriptor 分配失败），
        // 调用方保留 fallback 行为。
        bool DispatchDraw(const VertexArray* vertexArray,
                          VulkanShader*      shader,
                          const DrawParams&  params,
                          uint32_t           frameIndex);

    private:
        struct FrameResources
        {
            // GlobalUBO + LightsUBO 共用一个 host-visible persistent-mapped buffer 两段
            VkBuffer      GlobalBuffer     = VK_NULL_HANDLE;
            VmaAllocation GlobalAllocation = nullptr;
            void*         GlobalMapped     = nullptr;

            // 材质参数 ring（每 draw 一段，帧首重置）
            VkBuffer      MaterialBuffer     = VK_NULL_HANDLE;
            VmaAllocation MaterialAllocation = nullptr;
            void*         MaterialMapped     = nullptr;
            uint32_t      MaterialOffset     = 0;

            VkDescriptorSetLayout GlobalSetLayout = VK_NULL_HANDLE; // set0 占位（alloc 用实际 shader layout）
            VkDescriptorPool      Pool            = VK_NULL_HANDLE;
        };

        void     CreateFrameResources(uint32_t frameIndex);
        void     PackAndUploadGlobals(VulkanShader* shader, uint32_t frameIndex);
        uint32_t PackMaterial(VulkanShader* shader, uint32_t frameIndex); // 返回 ring 偏移；满时返回 UINT32_MAX

        // 1x1 白色占位纹理：未绑定槽位的 descriptor 兜底（避免空 descriptor 触发
        // device lost；采样结果为白，配合 u_HasXxxMap 开关语义无害）
        struct PlaceholderTexture
        {
            VkImage       Image      = VK_NULL_HANDLE;
            VmaAllocation Allocation = nullptr;
            VkImageView   View       = VK_NULL_HANDLE;
        };

        void CreatePlaceholder();
        void DestroyPlaceholder();

        std::array<FrameResources, kMaxFramesInFlight> m_Frames{};
        PlaceholderTexture                             m_Placeholder{};
        VkDevice                                       m_Device      = VK_NULL_HANDLE;
        bool                                           m_Initialized = false;
    };

} // namespace Engine
