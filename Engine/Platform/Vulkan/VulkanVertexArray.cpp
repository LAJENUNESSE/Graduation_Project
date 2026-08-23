#include "engpch.h"
#include "Platform/Vulkan/VulkanVertexArray.h"

#include "Platform/Vulkan/VulkanContext.h"

#include "Core/Assert.h"
#include "Core/Log.h"

namespace Engine
{
    namespace
    {
        // ShaderDataType -> VkFormat；只覆盖网格顶点属性用到的标量/向量类型。
        // 矩阵 / Bool 等顶点属性 Vulkan 路径暂不支持，返回 UNDEFINED 由调用方 warn。
        VkFormat ShaderDataTypeToVkFormat(ShaderDataType type)
        {
            switch (type)
            {
            case ShaderDataType::Float:
                return VK_FORMAT_R32_SFLOAT;
            case ShaderDataType::Float2:
                return VK_FORMAT_R32G32_SFLOAT;
            case ShaderDataType::Float3:
                return VK_FORMAT_R32G32B32_SFLOAT;
            case ShaderDataType::Float4:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case ShaderDataType::Int:
                return VK_FORMAT_R32_SINT;
            case ShaderDataType::Int2:
                return VK_FORMAT_R32G32_SINT;
            case ShaderDataType::Int3:
                return VK_FORMAT_R32G32B32_SINT;
            case ShaderDataType::Int4:
                return VK_FORMAT_R32G32B32A32_SINT;
            case ShaderDataType::Mat3:
            case ShaderDataType::Mat4:
            case ShaderDataType::Bool:
            case ShaderDataType::None:
                return VK_FORMAT_UNDEFINED;
            }

            ENGINE_CORE_ASSERT(false, "Unknown ShaderDataType!");
            return VK_FORMAT_UNDEFINED;
        }
    } // namespace

    // Phase 8.2：注册到场景状态机（DrawArrays 录制时消费）
    void VulkanVertexArray::Bind() const
    {
        if (auto* context = VulkanContext::Get())
            context->GetSceneState().SetCurrentVertexArray(this);
    }

    void VulkanVertexArray::Unbind() const {}

    void VulkanVertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
    {
        ENGINE_CORE_ASSERT(vertexBuffer && vertexBuffer->GetLayout().GetElements().size(),
                           "Vertex buffer must have a non-empty layout!");

        m_VertexBuffers.push_back(vertexBuffer);
    }

    void VulkanVertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
    {
        m_IndexBuffer = indexBuffer;
    }

    std::vector<VkVertexInputBindingDescription> VulkanVertexArray::BuildBindingDescriptions() const
    {
        std::vector<VkVertexInputBindingDescription> bindings;
        bindings.reserve(m_VertexBuffers.size());

        for (uint32_t i = 0; i < m_VertexBuffers.size(); ++i)
        {
            VkVertexInputBindingDescription binding{};
            binding.binding   = i;
            binding.stride    = m_VertexBuffers[i]->GetLayout().GetStride();
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            bindings.push_back(binding);
        }

        return bindings;
    }

    std::vector<VkVertexInputAttributeDescription> VulkanVertexArray::BuildAttributeDescriptions() const
    {
        std::vector<VkVertexInputAttributeDescription> attributes;

        uint32_t location = 0;
        for (uint32_t binding = 0; binding < m_VertexBuffers.size(); ++binding)
        {
            const BufferLayout& layout = m_VertexBuffers[binding]->GetLayout();
            for (const BufferElement& element : layout)
            {
                const VkFormat format = ShaderDataTypeToVkFormat(element.Type);
                if (format == VK_FORMAT_UNDEFINED)
                {
                    static bool warnedUnsupported = false;
                    if (!warnedUnsupported)
                    {
                        warnedUnsupported = true;
                        ENGINE_CORE_WARN("[Vulkan] VulkanVertexArray skips unsupported vertex attribute type");
                    }
                    continue;
                }

                VkVertexInputAttributeDescription attribute{};
                attribute.location = location++;
                attribute.binding  = binding;
                attribute.format   = format;
                attribute.offset   = element.Offset;
                attributes.push_back(attribute);
            }
        }

        return attributes;
    }

} // namespace Engine
