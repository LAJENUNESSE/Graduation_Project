#pragma once

#include "Debug/PerformanceMonitor.h"
#include "Renderer/RendererAPI.h"

namespace Engine
{

    class RenderCommand
    {
    public:
        static void Init();
        static void CreateRendererAPI();

        static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            s_RendererAPI->SetViewport(x, y, width, height);
        }

        static void SetClearColor(const glm::vec4& color) { s_RendererAPI->SetClearColor(color); }

        static void Clear() { s_RendererAPI->Clear(); }

        static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0)
        {
            s_RendererAPI->DrawIndexed(vertexArray, indexCount);

            // Performance stats tracking
            uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
            auto&    stats = PerformanceMonitor::Get().GetStats();
            stats.DrawCalls++;
            stats.Vertices += count;
            stats.Triangles += count / 3;
        }

        static void DrawArrays(uint32_t count, uint32_t first = 0) { s_RendererAPI->DrawArrays(count, first); }

        static void DrawArraysInstanced(uint32_t count, uint32_t instanceCount, uint32_t first = 0)
        {
            s_RendererAPI->DrawArraysInstanced(count, instanceCount, first);

            auto& stats = PerformanceMonitor::Get().GetStats();
            stats.DrawCalls++;
            stats.Vertices += count * instanceCount;
            stats.Triangles += (count / 3) * instanceCount;
        }

        static void DrawLines(uint32_t count, uint32_t first = 0) { s_RendererAPI->DrawLines(count, first); }

        static void SetDepthTest(bool enable) { s_RendererAPI->SetDepthTest(enable); }

        static void SetDepthFunc(DepthFunc func) { s_RendererAPI->SetDepthFunc(func); }

        static void SetCullFace(bool enable) { s_RendererAPI->SetCullFace(enable); }

        static void SetCullFaceMode(CullFaceMode mode) { s_RendererAPI->SetCullFaceMode(mode); }

        static void SetLineWidth(float width) { s_RendererAPI->SetLineWidth(width); }

        static void BindTextureUnit(uint32_t slot, uint32_t textureID)
        {
            s_RendererAPI->BindTextureUnit(slot, textureID);
        }

        static void BindCubemapUnit(uint32_t slot, uint32_t textureID)
        {
            s_RendererAPI->BindCubemapUnit(slot, textureID);
        }

        // Vulkan path PBR IBL 资源绑定（OpenGL 默认 warn-once 不应被走到）。
        // view / sampler 是 void* 透传 (VkImageView / VkSampler)，避开 vulkan.h 泄漏。
        // 调用方 if (RendererAPI::GetAPI() == Vulkan) 走 BindXxxView，否则走 BindXxxUnit。
        static void BindCubemapView(uint32_t slot, void* view, void* sampler)
        {
            s_RendererAPI->BindCubemapView(slot, view, sampler);
        }
        static void BindTextureView(uint32_t slot, void* view, void* sampler)
        {
            s_RendererAPI->BindTextureView(slot, view, sampler);
        }

        static void ClearColorOnly() { s_RendererAPI->ClearColorOnly(); }

        static int GetBoundFramebufferID() { return s_RendererAPI->GetBoundFramebufferID(); }

        static void BindFramebufferByID(int id) { s_RendererAPI->BindFramebufferByID(id); }

        static void DispatchCompute(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1)
        {
            s_RendererAPI->DispatchCompute(groupsX, groupsY, groupsZ);
        }

        static void MemoryBarrier(uint32_t barriers) { s_RendererAPI->MemoryBarrier(barriers); }
        static void WaitIdle() { s_RendererAPI->WaitIdle(); }

        static void DrawArraysIndirect(uint32_t bufferID)
        {
            s_RendererAPI->DrawArraysIndirect(bufferID);

            auto& stats = PerformanceMonitor::Get().GetStats();
            stats.DrawCalls++;
        }

        static void SetDepthMask(bool enable) { s_RendererAPI->SetDepthMask(enable); }

        static void SetBlendFunc(BlendFactor src, BlendFactor dst) { s_RendererAPI->SetBlendFunc(src, dst); }
        static void SetBlend(bool enable) { s_RendererAPI->SetBlend(enable); }
        static bool GetBlendEnabled() { return s_RendererAPI->GetBlendEnabled(); }
        static void SetScissorTest(bool enable) { s_RendererAPI->SetScissorTest(enable); }
        static void SetColorMask(bool r, bool g, bool b, bool a) { s_RendererAPI->SetColorMask(r, g, b, a); }

        static glm::vec4 GetClearColor() { return s_RendererAPI->GetClearColor(); }

        static void SetReadBuffer(uint32_t attachment) { s_RendererAPI->SetReadBuffer(attachment); }
        static void SetDrawBuffer(uint32_t attachment) { s_RendererAPI->SetDrawBuffer(attachment); }
        static void SetDrawBuffers(uint32_t count, const uint32_t* attachments)
        {
            s_RendererAPI->SetDrawBuffers(count, attachments);
        }
        static void CopyFramebufferToTexture(uint32_t texID, uint32_t width, uint32_t height)
        {
            s_RendererAPI->CopyFramebufferToTexture(texID, width, height);
        }

        static void QueryCapabilities(RendererCapabilities& caps) { s_RendererAPI->QueryCapabilities(caps); }

    private:
        static Scope<RendererAPI> s_RendererAPI;
    };

} // namespace Engine
