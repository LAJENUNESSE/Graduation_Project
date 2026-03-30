#include "engpch.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Core/Log.h"

#include <fstream>

namespace Engine
{

    // ============================================================================
    // VulkanGraphicsPipelineBuilder
    // ============================================================================

    VulkanGraphicsPipelineBuilder::VulkanGraphicsPipelineBuilder()
    {
        // Enable dynamic viewport and scissor by default (common pattern)
        m_DynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
        m_DynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::SetShader(VulkanShader* shader)
    {
        m_Shader = shader;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::SetRenderPass(VkRenderPass renderPass,
                                                                                 uint32_t     subpass)
    {
        m_RenderPass = renderPass;
        m_Subpass    = subpass;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::SetVertexInput(
        const VulkanVertexInputDescription& desc)
    {
        m_VertexInput = desc;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::SetTopology(VkPrimitiveTopology topology)
    {
        m_Topology = topology;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::SetRasterization(
        const VulkanRasterizationConfig& config)
    {
        m_Rasterization = config;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::SetDepthStencil(
        const VulkanDepthStencilConfig& config)
    {
        m_DepthStencil = config;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::SetBlend(const VulkanBlendConfig& config)
    {
        m_Blend = config;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::SetMultisample(const VulkanMultisampleConfig& config)
    {
        m_Multisample = config;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::SetPipelineLayout(VkPipelineLayout layout)
    {
        m_PipelineLayout = layout;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::SetViewport(float width, float height)
    {
        m_ViewportWidth  = width;
        m_ViewportHeight = height;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::EnableDynamicState(VkDynamicState state)
    {
        // Avoid duplicates
        for (auto s : m_DynamicStates)
        {
            if (s == state) return *this;
        }
        m_DynamicStates.push_back(state);
        return *this;
    }

    VkPipeline VulkanGraphicsPipelineBuilder::Build()
    {
        auto* context = VulkanContext::Get();
        if (!context)
        {
            ENGINE_CORE_ERROR("[VulkanPipeline] No VulkanContext available");
            return VK_NULL_HANDLE;
        }

        VkDevice device = context->GetDevice();

        // Validate required fields
        if (!m_Shader)
        {
            ENGINE_CORE_ERROR("[VulkanPipeline] Shader is required");
            return VK_NULL_HANDLE;
        }
        if (m_RenderPass == VK_NULL_HANDLE)
        {
            ENGINE_CORE_ERROR("[VulkanPipeline] RenderPass is required");
            return VK_NULL_HANDLE;
        }
        if (m_PipelineLayout == VK_NULL_HANDLE)
        {
            ENGINE_CORE_ERROR("[VulkanPipeline] PipelineLayout is required");
            return VK_NULL_HANDLE;
        }

        // Shader stages
        auto shaderStages = m_Shader->GetShaderStages();
        if (shaderStages.empty())
        {
            ENGINE_CORE_ERROR("[VulkanPipeline] Shader has no stages");
            return VK_NULL_HANDLE;
        }

        // Vertex input state
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount =
            static_cast<uint32_t>(m_VertexInput.bindings.size());
        vertexInputInfo.pVertexBindingDescriptions = m_VertexInput.bindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(m_VertexInput.attributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = m_VertexInput.attributes.data();

        // Input assembly state
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology               = m_Topology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport state (dynamic, but we still need to specify count)
        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = m_ViewportWidth > 0 ? m_ViewportWidth : 1.0f;
        viewport.height   = m_ViewportHeight > 0 ? m_ViewportHeight : 1.0f;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {static_cast<uint32_t>(viewport.width), static_cast<uint32_t>(viewport.height)};

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports    = &viewport;
        viewportState.scissorCount  = 1;
        viewportState.pScissors     = &scissor;

        // Rasterization state
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable        = m_Rasterization.depthClampEnable ? VK_TRUE : VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode             = m_Rasterization.polygonMode;
        rasterizer.lineWidth               = m_Rasterization.lineWidth;
        rasterizer.cullMode                = m_Rasterization.cullMode;
        rasterizer.frontFace               = m_Rasterization.frontFace;
        rasterizer.depthBiasEnable         = VK_FALSE;

        // Multisampling state
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable   = m_Multisample.sampleShadingEnable ? VK_TRUE : VK_FALSE;
        multisampling.rasterizationSamples  = m_Multisample.rasterizationSamples;
        multisampling.minSampleShading      = m_Multisample.minSampleShading;
        multisampling.pSampleMask           = nullptr;
        multisampling.alphaToCoverageEnable = VK_FALSE;
        multisampling.alphaToOneEnable      = VK_FALSE;

        // Depth stencil state
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable       = m_DepthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable      = m_DepthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp        = m_DepthStencil.depthCompareOp;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable     = m_DepthStencil.stencilEnable ? VK_TRUE : VK_FALSE;
        depthStencil.front                 = m_DepthStencil.front;
        depthStencil.back                  = m_DepthStencil.back;

        // Color blend attachment state
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask      = m_Blend.colorWriteMask;
        colorBlendAttachment.blendEnable         = m_Blend.blendEnable ? VK_TRUE : VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = m_Blend.srcColorBlendFactor;
        colorBlendAttachment.dstColorBlendFactor = m_Blend.dstColorBlendFactor;
        colorBlendAttachment.colorBlendOp        = m_Blend.colorBlendOp;
        colorBlendAttachment.srcAlphaBlendFactor = m_Blend.srcAlphaBlendFactor;
        colorBlendAttachment.dstAlphaBlendFactor = m_Blend.dstAlphaBlendFactor;
        colorBlendAttachment.alphaBlendOp        = m_Blend.alphaBlendOp;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable     = VK_FALSE;
        colorBlending.logicOp           = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount   = 1;
        colorBlending.pAttachments      = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;

        // Dynamic state
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(m_DynamicStates.size());
        dynamicState.pDynamicStates    = m_DynamicStates.data();

        // Create the pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages             = shaderStages.data();
        pipelineInfo.pVertexInputState   = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState   = &multisampling;
        pipelineInfo.pDepthStencilState  = &depthStencil;
        pipelineInfo.pColorBlendState    = &colorBlending;
        pipelineInfo.pDynamicState       = &dynamicState;
        pipelineInfo.layout              = m_PipelineLayout;
        pipelineInfo.renderPass          = m_RenderPass;
        pipelineInfo.subpass             = m_Subpass;
        pipelineInfo.basePipelineHandle  = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex   = -1;

        VkPipeline pipeline;
        VkResult   result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[VulkanPipeline] Failed to create graphics pipeline: {}", static_cast<int>(result));
            return VK_NULL_HANDLE;
        }

        ENGINE_CORE_TRACE("[VulkanPipeline] Created graphics pipeline for shader '{}'", m_Shader->GetName());
        return pipeline;
    }

    // ============================================================================
    // VulkanComputePipelineBuilder
    // ============================================================================

    VulkanComputePipelineBuilder::VulkanComputePipelineBuilder() = default;

    VulkanComputePipelineBuilder& VulkanComputePipelineBuilder::SetShader(VulkanShader* shader)
    {
        m_Shader = shader;
        return *this;
    }

    VulkanComputePipelineBuilder& VulkanComputePipelineBuilder::SetPipelineLayout(VkPipelineLayout layout)
    {
        m_PipelineLayout = layout;
        return *this;
    }

    VkPipeline VulkanComputePipelineBuilder::Build()
    {
        auto* context = VulkanContext::Get();
        if (!context)
        {
            ENGINE_CORE_ERROR("[VulkanComputePipeline] No VulkanContext available");
            return VK_NULL_HANDLE;
        }

        VkDevice device = context->GetDevice();

        if (!m_Shader || !m_Shader->IsCompute())
        {
            ENGINE_CORE_ERROR("[VulkanComputePipeline] Valid compute shader is required");
            return VK_NULL_HANDLE;
        }
        if (m_PipelineLayout == VK_NULL_HANDLE)
        {
            ENGINE_CORE_ERROR("[VulkanComputePipeline] PipelineLayout is required");
            return VK_NULL_HANDLE;
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = m_Shader->GetComputeModule();
        stageInfo.pName  = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage  = stageInfo;
        pipelineInfo.layout = m_PipelineLayout;

        VkPipeline pipeline;
        VkResult   result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[VulkanComputePipeline] Failed to create compute pipeline: {}",
                              static_cast<int>(result));
            return VK_NULL_HANDLE;
        }

        ENGINE_CORE_TRACE("[VulkanComputePipeline] Created compute pipeline for shader '{}'", m_Shader->GetName());
        return pipeline;
    }

    // ============================================================================
    // VulkanPipelineLayoutBuilder
    // ============================================================================

    VulkanPipelineLayoutBuilder::VulkanPipelineLayoutBuilder() = default;

    VulkanPipelineLayoutBuilder& VulkanPipelineLayoutBuilder::AddDescriptorSetLayout(VkDescriptorSetLayout layout)
    {
        m_SetLayouts.push_back(layout);
        return *this;
    }

    VulkanPipelineLayoutBuilder& VulkanPipelineLayoutBuilder::AddPushConstantRange(VkShaderStageFlags stageFlags,
                                                                                    uint32_t           offset,
                                                                                    uint32_t           size)
    {
        VkPushConstantRange range{};
        range.stageFlags = stageFlags;
        range.offset     = offset;
        range.size       = size;
        m_PushConstantRanges.push_back(range);
        return *this;
    }

    VkPipelineLayout VulkanPipelineLayoutBuilder::Build()
    {
        auto* context = VulkanContext::Get();
        if (!context)
        {
            ENGINE_CORE_ERROR("[VulkanPipelineLayout] No VulkanContext available");
            return VK_NULL_HANDLE;
        }

        VkDevice device = context->GetDevice();

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount         = static_cast<uint32_t>(m_SetLayouts.size());
        layoutInfo.pSetLayouts            = m_SetLayouts.empty() ? nullptr : m_SetLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(m_PushConstantRanges.size());
        layoutInfo.pPushConstantRanges    = m_PushConstantRanges.empty() ? nullptr : m_PushConstantRanges.data();

        VkPipelineLayout layout;
        VkResult         result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[VulkanPipelineLayout] Failed to create pipeline layout: {}", static_cast<int>(result));
            return VK_NULL_HANDLE;
        }

        ENGINE_CORE_TRACE("[VulkanPipelineLayout] Created pipeline layout with {} set layouts, {} push constants",
                          m_SetLayouts.size(), m_PushConstantRanges.size());
        return layout;
    }

    // ============================================================================
    // VulkanPipelineCache
    // ============================================================================

    VulkanPipelineCache::VulkanPipelineCache() = default;

    VulkanPipelineCache::~VulkanPipelineCache()
    {
        Shutdown();
    }

    void VulkanPipelineCache::Init()
    {
        auto* context = VulkanContext::Get();
        if (!context) return;

        VkDevice device = context->GetDevice();

        VkPipelineCacheCreateInfo cacheInfo{};
        cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

        VkResult result = vkCreatePipelineCache(device, &cacheInfo, nullptr, &m_Cache);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_WARN("[VulkanPipelineCache] Failed to create pipeline cache");
        }
    }

    void VulkanPipelineCache::Shutdown()
    {
        if (m_Cache != VK_NULL_HANDLE)
        {
            auto* context = VulkanContext::Get();
            if (context)
            {
                vkDestroyPipelineCache(context->GetDevice(), m_Cache, nullptr);
            }
            m_Cache = VK_NULL_HANDLE;
        }
    }

    void VulkanPipelineCache::SaveToFile(const std::string& filepath)
    {
        if (m_Cache == VK_NULL_HANDLE) return;

        auto* context = VulkanContext::Get();
        if (!context) return;

        VkDevice device = context->GetDevice();

        size_t cacheSize = 0;
        vkGetPipelineCacheData(device, m_Cache, &cacheSize, nullptr);

        if (cacheSize == 0) return;

        std::vector<char> cacheData(cacheSize);
        vkGetPipelineCacheData(device, m_Cache, &cacheSize, cacheData.data());

        std::ofstream file(filepath, std::ios::binary);
        if (file.is_open())
        {
            file.write(cacheData.data(), cacheSize);
            ENGINE_CORE_TRACE("[VulkanPipelineCache] Saved {} bytes to {}", cacheSize, filepath);
        }
    }

    void VulkanPipelineCache::LoadFromFile(const std::string& filepath)
    {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return;

        size_t            fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> cacheData(fileSize);
        file.seekg(0);
        file.read(cacheData.data(), fileSize);

        auto* context = VulkanContext::Get();
        if (!context) return;

        VkDevice device = context->GetDevice();

        // Destroy old cache and create new one with loaded data
        if (m_Cache != VK_NULL_HANDLE)
        {
            vkDestroyPipelineCache(device, m_Cache, nullptr);
        }

        VkPipelineCacheCreateInfo cacheInfo{};
        cacheInfo.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        cacheInfo.initialDataSize = cacheData.size();
        cacheInfo.pInitialData    = cacheData.data();

        VkResult result = vkCreatePipelineCache(device, &cacheInfo, nullptr, &m_Cache);
        if (result == VK_SUCCESS)
        {
            ENGINE_CORE_TRACE("[VulkanPipelineCache] Loaded {} bytes from {}", fileSize, filepath);
        }
    }

    // ============================================================================
    // VulkanVertexLayouts
    // ============================================================================

    namespace VulkanVertexLayouts
    {
        VulkanVertexInputDescription GetPBRVertexLayout()
        {
            VulkanVertexInputDescription desc;

            // Single binding for interleaved vertex data
            VkVertexInputBindingDescription binding{};
            binding.binding   = 0;
            binding.stride    = sizeof(float) * (3 + 3 + 2 + 3); // pos + normal + uv + tangent
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            desc.bindings.push_back(binding);

            // Position (location 0)
            VkVertexInputAttributeDescription posAttr{};
            posAttr.binding  = 0;
            posAttr.location = 0;
            posAttr.format   = VK_FORMAT_R32G32B32_SFLOAT;
            posAttr.offset   = 0;
            desc.attributes.push_back(posAttr);

            // Normal (location 1)
            VkVertexInputAttributeDescription normalAttr{};
            normalAttr.binding  = 0;
            normalAttr.location = 1;
            normalAttr.format   = VK_FORMAT_R32G32B32_SFLOAT;
            normalAttr.offset   = sizeof(float) * 3;
            desc.attributes.push_back(normalAttr);

            // TexCoord (location 2)
            VkVertexInputAttributeDescription uvAttr{};
            uvAttr.binding  = 0;
            uvAttr.location = 2;
            uvAttr.format   = VK_FORMAT_R32G32_SFLOAT;
            uvAttr.offset   = sizeof(float) * 6;
            desc.attributes.push_back(uvAttr);

            // Tangent (location 3)
            VkVertexInputAttributeDescription tangentAttr{};
            tangentAttr.binding  = 0;
            tangentAttr.location = 3;
            tangentAttr.format   = VK_FORMAT_R32G32B32_SFLOAT;
            tangentAttr.offset   = sizeof(float) * 8;
            desc.attributes.push_back(tangentAttr);

            return desc;
        }

        VulkanVertexInputDescription GetSimpleVertexLayout()
        {
            VulkanVertexInputDescription desc;

            VkVertexInputBindingDescription binding{};
            binding.binding   = 0;
            binding.stride    = sizeof(float) * 5; // pos (3) + uv (2)
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            desc.bindings.push_back(binding);

            VkVertexInputAttributeDescription posAttr{};
            posAttr.binding  = 0;
            posAttr.location = 0;
            posAttr.format   = VK_FORMAT_R32G32B32_SFLOAT;
            posAttr.offset   = 0;
            desc.attributes.push_back(posAttr);

            VkVertexInputAttributeDescription uvAttr{};
            uvAttr.binding  = 0;
            uvAttr.location = 1;
            uvAttr.format   = VK_FORMAT_R32G32_SFLOAT;
            uvAttr.offset   = sizeof(float) * 3;
            desc.attributes.push_back(uvAttr);

            return desc;
        }

        VulkanVertexInputDescription GetPositionOnlyLayout()
        {
            VulkanVertexInputDescription desc;

            VkVertexInputBindingDescription binding{};
            binding.binding   = 0;
            binding.stride    = sizeof(float) * 3;
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            desc.bindings.push_back(binding);

            VkVertexInputAttributeDescription posAttr{};
            posAttr.binding  = 0;
            posAttr.location = 0;
            posAttr.format   = VK_FORMAT_R32G32B32_SFLOAT;
            posAttr.offset   = 0;
            desc.attributes.push_back(posAttr);

            return desc;
        }

        VulkanVertexInputDescription GetScreenQuadLayout()
        {
            VulkanVertexInputDescription desc;

            VkVertexInputBindingDescription binding{};
            binding.binding   = 0;
            binding.stride    = sizeof(float) * 4; // pos (2) + uv (2)
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            desc.bindings.push_back(binding);

            VkVertexInputAttributeDescription posAttr{};
            posAttr.binding  = 0;
            posAttr.location = 0;
            posAttr.format   = VK_FORMAT_R32G32_SFLOAT;
            posAttr.offset   = 0;
            desc.attributes.push_back(posAttr);

            VkVertexInputAttributeDescription uvAttr{};
            uvAttr.binding  = 0;
            uvAttr.location = 1;
            uvAttr.format   = VK_FORMAT_R32G32_SFLOAT;
            uvAttr.offset   = sizeof(float) * 2;
            desc.attributes.push_back(uvAttr);

            return desc;
        }

        VulkanVertexInputDescription GetParticleVertexLayout()
        {
            VulkanVertexInputDescription desc;

            VkVertexInputBindingDescription binding{};
            binding.binding   = 0;
            binding.stride    = sizeof(float) * 8; // pos (3) + color (4) + size (1)
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            desc.bindings.push_back(binding);

            VkVertexInputAttributeDescription posAttr{};
            posAttr.binding  = 0;
            posAttr.location = 0;
            posAttr.format   = VK_FORMAT_R32G32B32_SFLOAT;
            posAttr.offset   = 0;
            desc.attributes.push_back(posAttr);

            VkVertexInputAttributeDescription colorAttr{};
            colorAttr.binding  = 0;
            colorAttr.location = 1;
            colorAttr.format   = VK_FORMAT_R32G32B32A32_SFLOAT;
            colorAttr.offset   = sizeof(float) * 3;
            desc.attributes.push_back(colorAttr);

            VkVertexInputAttributeDescription sizeAttr{};
            sizeAttr.binding  = 0;
            sizeAttr.location = 2;
            sizeAttr.format   = VK_FORMAT_R32_SFLOAT;
            sizeAttr.offset   = sizeof(float) * 7;
            desc.attributes.push_back(sizeAttr);

            return desc;
        }
    }

} // namespace Engine
