#include "engpch.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include "Core/Log.h"

namespace Engine
{

    void VulkanRendererAPI::Init()
    {
        ENGINE_CORE_INFO("VulkanRendererAPI::Init() - Stub");
    }

    void VulkanRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        // TODO: Implement viewport
    }

    void VulkanRendererAPI::SetClearColor(const glm::vec4& color)
    {
        m_ClearColor = color;
    }

    void VulkanRendererAPI::Clear()
    {
        // TODO: Implement clear
    }

    void VulkanRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
    {
        // TODO: Implement draw indexed
    }

    void VulkanRendererAPI::DrawArrays(uint32_t count, uint32_t first)
    {
        // TODO: Implement draw arrays
    }

    void VulkanRendererAPI::DrawArraysInstanced(uint32_t count, uint32_t instanceCount, uint32_t first)
    {
        // TODO: Implement draw arrays instanced
    }

    void VulkanRendererAPI::DrawLines(uint32_t count, uint32_t first)
    {
        // TODO: Implement draw lines
    }

    void VulkanRendererAPI::SetDepthTest(bool enable)
    {
        // TODO: Implement depth test
    }

    void VulkanRendererAPI::SetDepthFunc(DepthFunc func)
    {
        // TODO: Implement depth func
    }

    void VulkanRendererAPI::SetCullFace(bool enable)
    {
        // TODO: Implement cull face
    }

    void VulkanRendererAPI::SetCullFaceMode(CullFaceMode mode)
    {
        // TODO: Implement cull face mode
    }

    void VulkanRendererAPI::SetLineWidth(float width)
    {
        // TODO: Implement line width
    }

    void VulkanRendererAPI::BindTextureUnit(uint32_t slot, uint32_t textureID)
    {
        // TODO: Implement bind texture
    }

    void VulkanRendererAPI::BindCubemapUnit(uint32_t slot, uint32_t textureID)
    {
        // TODO: Implement bind cubemap
    }

    void VulkanRendererAPI::ClearColorOnly()
    {
        // TODO: Implement clear color only
    }

    int VulkanRendererAPI::GetBoundFramebufferID()
    {
        // TODO: Implement get bound framebuffer
        return 0;
    }

    void VulkanRendererAPI::BindFramebufferByID(int id)
    {
        // TODO: Implement bind framebuffer
    }

    void VulkanRendererAPI::DispatchCompute(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        // TODO: Implement dispatch compute
    }

    void VulkanRendererAPI::MemoryBarrier(uint32_t barriers)
    {
        // TODO: Implement memory barrier
    }

    void VulkanRendererAPI::DrawArraysIndirect(uint32_t bufferID)
    {
        // TODO: Implement draw arrays indirect
    }

    void VulkanRendererAPI::SetDepthMask(bool enable)
    {
        // TODO: Implement depth mask
    }

    void VulkanRendererAPI::SetBlendFunc(BlendFactor src, BlendFactor dst)
    {
        // TODO: Implement blend func
    }

} // namespace Engine
