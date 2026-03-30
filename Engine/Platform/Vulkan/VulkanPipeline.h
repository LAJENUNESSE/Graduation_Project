#pragma once

#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace Engine
{
    class VulkanShader;

    // ============================================================================
    // Pipeline configuration structures
    // ============================================================================

    struct VulkanVertexInputDescription
    {
        std::vector<VkVertexInputBindingDescription>   bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
    };

    // Rasterization state configuration
    struct VulkanRasterizationConfig
    {
        VkPolygonMode   polygonMode     = VK_POLYGON_MODE_FILL;
        VkCullModeFlags cullMode        = VK_CULL_MODE_BACK_BIT;
        VkFrontFace     frontFace       = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        float           lineWidth       = 1.0f;
        bool            depthClampEnable = false;
    };

    // Depth stencil configuration
    struct VulkanDepthStencilConfig
    {
        bool             depthTestEnable  = true;
        bool             depthWriteEnable = true;
        VkCompareOp      depthCompareOp   = VK_COMPARE_OP_LESS;
        bool             stencilEnable    = false;
        VkStencilOpState front            = {};
        VkStencilOpState back             = {};
    };

    // Blend attachment configuration
    struct VulkanBlendConfig
    {
        bool            blendEnable         = false;
        VkBlendFactor   srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        VkBlendFactor   dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkBlendOp       colorBlendOp        = VK_BLEND_OP_ADD;
        VkBlendFactor   srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        VkBlendFactor   dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        VkBlendOp       alphaBlendOp        = VK_BLEND_OP_ADD;
        VkColorComponentFlags colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    };

    // Multisampling configuration
    struct VulkanMultisampleConfig
    {
        VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        bool                  sampleShadingEnable  = false;
        float                 minSampleShading     = 1.0f;
    };

    // ============================================================================
    // VulkanGraphicsPipelineBuilder - Fluent builder for graphics pipelines
    // ============================================================================
    class VulkanGraphicsPipelineBuilder
    {
    public:
        VulkanGraphicsPipelineBuilder();

        VulkanGraphicsPipelineBuilder& SetShader(VulkanShader* shader);
        VulkanGraphicsPipelineBuilder& SetRenderPass(VkRenderPass renderPass, uint32_t subpass = 0);
        VulkanGraphicsPipelineBuilder& SetVertexInput(const VulkanVertexInputDescription& desc);
        VulkanGraphicsPipelineBuilder& SetTopology(VkPrimitiveTopology topology);
        VulkanGraphicsPipelineBuilder& SetRasterization(const VulkanRasterizationConfig& config);
        VulkanGraphicsPipelineBuilder& SetDepthStencil(const VulkanDepthStencilConfig& config);
        VulkanGraphicsPipelineBuilder& SetBlend(const VulkanBlendConfig& config);
        VulkanGraphicsPipelineBuilder& SetMultisample(const VulkanMultisampleConfig& config);
        VulkanGraphicsPipelineBuilder& SetPipelineLayout(VkPipelineLayout layout);
        VulkanGraphicsPipelineBuilder& SetViewport(float width, float height);
        VulkanGraphicsPipelineBuilder& EnableDynamicState(VkDynamicState state);

        // Build the pipeline - returns VK_NULL_HANDLE on failure
        VkPipeline Build();

    private:
        VulkanShader*                m_Shader       = nullptr;
        VkRenderPass                 m_RenderPass   = VK_NULL_HANDLE;
        uint32_t                     m_Subpass      = 0;
        VkPipelineLayout             m_PipelineLayout = VK_NULL_HANDLE;
        VulkanVertexInputDescription m_VertexInput;
        VkPrimitiveTopology          m_Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VulkanRasterizationConfig    m_Rasterization;
        VulkanDepthStencilConfig     m_DepthStencil;
        VulkanBlendConfig            m_Blend;
        VulkanMultisampleConfig      m_Multisample;
        float                        m_ViewportWidth  = 0.0f;
        float                        m_ViewportHeight = 0.0f;
        std::vector<VkDynamicState>  m_DynamicStates;
    };

    // ============================================================================
    // VulkanComputePipelineBuilder - Builder for compute pipelines
    // ============================================================================
    class VulkanComputePipelineBuilder
    {
    public:
        VulkanComputePipelineBuilder();

        VulkanComputePipelineBuilder& SetShader(VulkanShader* shader);
        VulkanComputePipelineBuilder& SetPipelineLayout(VkPipelineLayout layout);

        // Build the compute pipeline
        VkPipeline Build();

    private:
        VulkanShader*    m_Shader         = nullptr;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    };

    // ============================================================================
    // VulkanPipelineLayoutBuilder - Builder for pipeline layouts
    // ============================================================================
    class VulkanPipelineLayoutBuilder
    {
    public:
        VulkanPipelineLayoutBuilder();

        VulkanPipelineLayoutBuilder& AddDescriptorSetLayout(VkDescriptorSetLayout layout);
        VulkanPipelineLayoutBuilder& AddPushConstantRange(VkShaderStageFlags stageFlags, uint32_t offset,
                                                          uint32_t size);

        VkPipelineLayout Build();

    private:
        std::vector<VkDescriptorSetLayout> m_SetLayouts;
        std::vector<VkPushConstantRange>   m_PushConstantRanges;
    };

    // ============================================================================
    // VulkanPipelineCache - Cache for avoiding redundant pipeline compilation
    // ============================================================================
    class VulkanPipelineCache
    {
    public:
        VulkanPipelineCache();
        ~VulkanPipelineCache();

        void Init();
        void Shutdown();

        VkPipelineCache GetCache() const { return m_Cache; }

        // Save cache to disk for faster subsequent loads
        void SaveToFile(const std::string& filepath);
        void LoadFromFile(const std::string& filepath);

    private:
        VkPipelineCache m_Cache = VK_NULL_HANDLE;
    };

    // ============================================================================
    // Utility functions for common vertex layouts
    // ============================================================================
    namespace VulkanVertexLayouts
    {
        // Standard PBR vertex: position (vec3), normal (vec3), texCoord (vec2), tangent (vec3)
        VulkanVertexInputDescription GetPBRVertexLayout();

        // Simple vertex: position (vec3), texCoord (vec2)
        VulkanVertexInputDescription GetSimpleVertexLayout();

        // Position-only: position (vec3)
        VulkanVertexInputDescription GetPositionOnlyLayout();

        // Screen quad: position (vec2), texCoord (vec2)
        VulkanVertexInputDescription GetScreenQuadLayout();

        // Particle vertex: position (vec3), color (vec4), size (float)
        VulkanVertexInputDescription GetParticleVertexLayout();
    }

} // namespace Engine
