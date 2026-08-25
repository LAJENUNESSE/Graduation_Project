#include "engpch.h"
#include "Platform/Vulkan/VulkanGraphicsPipelineBuilder.h"

#include <algorithm>

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanDescriptor.h"

namespace Engine
{

    VulkanPipelineCache::PipelineHandle VulkanGraphicsPipelineBuilder::GetOrCreate(VkDevice                    device,
                                                                                   const GraphicsPipelineDesc& desc)
    {
        ENGINE_CORE_RELEASE_ASSERT(device != VK_NULL_HANDLE, "VulkanGraphicsPipelineBuilder requires a device");
        ENGINE_CORE_RELEASE_ASSERT(desc.Shader != nullptr, "GraphicsPipelineDesc.Shader must be set");
        ENGINE_CORE_RELEASE_ASSERT(desc.RenderPass != VK_NULL_HANDLE, "GraphicsPipelineDesc.RenderPass must be set");

        VulkanGraphicsPipelineKey key{};
        key.Shader               = desc.Shader;
        key.RenderPass           = desc.RenderPass;
        key.VertexLayoutHash     = ComputeVertexLayoutHash(desc);
        key.ColorAttachmentCount = desc.ColorAttachmentCount;
        key.StateBits            = ComputeStateBits(desc);

        return m_Cache.GetOrCreate(
            key,
            [this, device, &desc]() -> VulkanPipelineCache::PipelineHandle
            {
                VkPipelineLayout layout = GetOrCreatePipelineLayout(device, desc.Shader);
                if (layout == VK_NULL_HANDLE)
                    return {};

                // ---- shader stages ----
                std::vector<VkPipelineShaderStageCreateInfo> stages;
                const VkShaderModule vertexModule = desc.Shader->GetOrCreateShaderModule(device, "vertex");
                ENGINE_CORE_RELEASE_ASSERT(vertexModule != VK_NULL_HANDLE, "Shader has no vertex SPIR-V module");
                stages.push_back({VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                  VK_SHADER_STAGE_VERTEX_BIT, vertexModule, "main", nullptr});

                if (desc.Shader->HasStage("fragment"))
                {
                    const VkShaderModule fragmentModule = desc.Shader->GetOrCreateShaderModule(device, "fragment");
                    ENGINE_CORE_RELEASE_ASSERT(fragmentModule != VK_NULL_HANDLE, "Fragment SPIR-V module missing");
                    stages.push_back({VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                      VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule, "main", nullptr});
                }

                // ---- vertex input：过滤 shader 未消费的 attribute（如 Depth.glsl
                // 只读 location 0，而 mesh VAO 带 4 个 attribute）----
                std::vector<VkVertexInputAttributeDescription> consumedAttributes;
                const auto&                                    inputLocations = desc.Shader->GetVertexInputLocations();
                if (inputLocations.empty())
                {
                    consumedAttributes = desc.Attributes; // 无反射信息时不过滤（防御）
                }
                else
                {
                    for (const auto& attr : desc.Attributes)
                    {
                        if (std::find(inputLocations.begin(), inputLocations.end(), attr.location) !=
                            inputLocations.end())
                            consumedAttributes.push_back(attr);
                    }
                }

                VkPipelineVertexInputStateCreateInfo vertexInput{
                    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
                vertexInput.vertexBindingDescriptionCount   = static_cast<uint32_t>(desc.Bindings.size());
                vertexInput.pVertexBindingDescriptions      = desc.Bindings.empty() ? nullptr : desc.Bindings.data();
                vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(consumedAttributes.size());
                vertexInput.pVertexAttributeDescriptions =
                    consumedAttributes.empty() ? nullptr : consumedAttributes.data();

                // ---- input assembly ----
                VkPipelineInputAssemblyStateCreateInfo inputAssembly{
                    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
                inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

                // ---- dynamic viewport/scissor（录制时 vkCmdSet）----
                std::array<VkDynamicState, 2>    dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
                VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
                dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
                dynamicState.pDynamicStates    = dynamicStates.data();

                VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
                viewportState.viewportCount = 1;
                viewportState.scissorCount  = 1;

                // ---- rasterizer ----
                VkPipelineRasterizationStateCreateInfo rasterizer{
                    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
                rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                rasterizer.cullMode    = VK_CULL_MODE_NONE;
                if (desc.CullBack && !desc.CullFront)
                    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
                else if (!desc.CullBack && desc.CullFront)
                    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
                else if (desc.CullBack && desc.CullFront)
                    rasterizer.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
                // Vulkan 的 NDC Y 轴与 OpenGL 相反，GL 惯例的 CCW 正面顶点序在
                // Vulkan 里投影后变为 CW——统一以 CW 为正面才能保持与 GL 后端
                // 相同的剔除语义（否则开背面剔除的 pipeline 会剔光所有三角形）
                rasterizer.frontFace               = VK_FRONT_FACE_CLOCKWISE;
                rasterizer.lineWidth               = 1.0f;
                rasterizer.depthClampEnable        = VK_FALSE;
                rasterizer.rasterizerDiscardEnable = VK_FALSE;

                // ---- multisampling ----
                VkPipelineMultisampleStateCreateInfo multisampling{
                    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
                multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                // ---- depth/stencil ----
                VkPipelineDepthStencilStateCreateInfo depthStencil{
                    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
                depthStencil.depthTestEnable   = desc.DepthTest ? VK_TRUE : VK_FALSE;
                depthStencil.depthWriteEnable  = desc.DepthWrite ? VK_TRUE : VK_FALSE;
                depthStencil.depthCompareOp    = desc.DepthLEqual ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
                depthStencil.stencilTestEnable = VK_FALSE;

                // ---- blend：每个 color attachment 独立状态（不透明路径禁用混合）----
                std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(desc.ColorAttachmentCount);
                for (auto& blend : blendAttachments)
                {
                    blend.blendEnable    = VK_FALSE;
                    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                }
                VkPipelineColorBlendStateCreateInfo colorBlend{
                    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
                colorBlend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
                colorBlend.pAttachments    = blendAttachments.data();

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.stageCount          = static_cast<uint32_t>(stages.size());
                pipelineInfo.pStages             = stages.data();
                pipelineInfo.pVertexInputState   = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState      = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState   = &multisampling;
                pipelineInfo.pDepthStencilState  = &depthStencil;
                pipelineInfo.pColorBlendState    = &colorBlend;
                pipelineInfo.pDynamicState       = &dynamicState;
                pipelineInfo.layout              = layout;
                pipelineInfo.renderPass          = desc.RenderPass;
                pipelineInfo.subpass             = 0;

                VulkanPipelineCache::PipelineHandle handle{};
                const VkResult                      result =
                    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &handle.Pipeline);
                if (result != VK_SUCCESS)
                {
                    ENGINE_CORE_ERROR("[Vulkan] Failed to create scene graphics pipeline ({})",
                                      static_cast<int>(result));
                    return {};
                }
                handle.Layout = layout;
                return handle;
            });
    }

    const std::vector<Ref<VulkanDescriptorSetLayout>>*
    VulkanGraphicsPipelineBuilder::GetSetLayouts(VulkanShader* shader) const
    {
        auto it = m_ShaderResources.find(shader);
        return it != m_ShaderResources.end() ? &it->second.SetLayouts : nullptr;
    }

    VkPipelineLayout VulkanGraphicsPipelineBuilder::GetOrCreatePipelineLayout(VkDevice device, VulkanShader* shader)
    {
        auto it = m_ShaderResources.find(shader);
        if (it != m_ShaderResources.end())
            return it->second.Layout;

        ShaderResources resources;

        // ---- descriptor set layouts：反射结果按 set 下标分组 ----
        uint32_t maxSet = 0;
        for (const auto& binding : shader->GetReflectedBindings())
            maxSet = std::max(maxSet, binding.Set);

        resources.SetLayouts.resize(maxSet + 1);
        for (uint32_t set = 0; set <= maxSet; ++set)
        {
            // 每个 0..maxSet 都建 layout：无 binding 的 set 得到空占位 layout，
            // 保证 pipeline layout 的 pSetLayouts 下标与 set 编号一一对应。
            // （反射 set 从 0 连续时无影响；若未来出现空洞也不会错位）
            resources.SetLayouts[set] =
                VulkanDescriptorSetLayout::CreateFromReflection(device, shader->GetReflectedBindings(), set);
        }

        // ---- push constant ranges ----
        std::vector<VkPushConstantRange> pcRanges;
        for (const auto& pc : shader->GetReflectedPushConstants())
        {
            VkPushConstantRange range{};
            range.offset     = pc.Offset;
            range.size       = pc.Size;
            range.stageFlags = pc.Stages;
            pcRanges.push_back(range);
        }

        std::vector<VkDescriptorSetLayout> activeLayouts;
        for (const auto& layout : resources.SetLayouts)
            if (layout)
                activeLayouts.push_back(layout->GetHandle());

        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount         = static_cast<uint32_t>(activeLayouts.size());
        layoutInfo.pSetLayouts            = activeLayouts.empty() ? nullptr : activeLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pcRanges.size());
        layoutInfo.pPushConstantRanges    = pcRanges.empty() ? nullptr : pcRanges.data();

        const VkResult result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &resources.Layout);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create scene pipeline layout");

        m_ShaderResources.emplace(shader, std::move(resources));
        return m_ShaderResources[shader].Layout;
    }

    uint32_t VulkanGraphicsPipelineBuilder::ComputeStateBits(const GraphicsPipelineDesc& desc)
    {
        uint32_t bits = 0;
        if (desc.DepthTest)
            bits |= PipelineStateBits::kDepthTest;
        if (desc.DepthWrite)
            bits |= PipelineStateBits::kDepthWrite;
        if (desc.CullBack)
            bits |= PipelineStateBits::kCullBack;
        if (!desc.CullBack && !desc.CullFront)
            bits |= 0; // 双面：两位都清
        if (desc.CullFront)
            bits |= PipelineStateBits::kCullFront;
        if (desc.DepthLEqual)
            bits |= PipelineStateBits::kDepthLEqual;
        return bits;
    }

    uint64_t VulkanGraphicsPipelineBuilder::ComputeVertexLayoutHash(const GraphicsPipelineDesc& desc)
    {
        uint64_t h   = 14695981039346656037ull;
        auto     mix = [&h](uint64_t v)
        {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(desc.Bindings.size());
        for (const auto& b : desc.Bindings)
        {
            mix(b.binding);
            mix(b.stride);
            mix(static_cast<uint64_t>(b.inputRate));
        }
        mix(desc.Attributes.size());
        for (const auto& a : desc.Attributes)
        {
            mix(a.location);
            mix(a.binding);
            mix(static_cast<uint64_t>(a.format));
            mix(a.offset);
        }
        return h;
    }

    void VulkanGraphicsPipelineBuilder::ReleaseShader(VkDevice device, VulkanShader* shader)
    {
        auto it = m_ShaderResources.find(shader);
        if (it == m_ShaderResources.end())
            return;

        if (device != VK_NULL_HANDLE)
        {
            it->second.SetLayouts.clear();
            if (it->second.Layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(device, it->second.Layout, nullptr);
        }
        m_ShaderResources.erase(it);

        // 清除缓存中属于该 shader 的 pipeline 条目（key.Shader 指针比较）
        for (auto iter = m_Cache.Begin(); iter != m_Cache.End();)
        {
            if (iter->first.Shader == shader)
                iter = m_Cache.Erase(iter, device);
            else
                ++iter;
        }
    }

    void VulkanGraphicsPipelineBuilder::Clear(VkDevice device)
    {
        if (device != VK_NULL_HANDLE)
        {
            for (auto& [_, resources] : m_ShaderResources)
            {
                resources.SetLayouts.clear();
                if (resources.Layout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(device, resources.Layout, nullptr);
                    resources.Layout = VK_NULL_HANDLE;
                }
            }
        }
        m_ShaderResources.clear();
        m_Cache.Clear(device);
    }

} // namespace Engine
