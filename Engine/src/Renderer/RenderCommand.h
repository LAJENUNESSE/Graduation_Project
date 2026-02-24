#pragma once

#include "Renderer/RendererAPI.h"
#include "Debug/PerformanceMonitor.h"

namespace Engine
{

    class RenderCommand
    {
    public:
        static void Init()
        {
            s_RendererAPI->Init();
        }

        static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            s_RendererAPI->SetViewport(x, y, width, height);
        }

        static void SetClearColor(const glm::vec4& color)
        {
            s_RendererAPI->SetClearColor(color);
        }

        static void Clear()
        {
            s_RendererAPI->Clear();
        }

        static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0)
        {
            s_RendererAPI->DrawIndexed(vertexArray, indexCount);

            // Performance stats tracking
            uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
            auto& stats = PerformanceMonitor::Get().GetStats();
            stats.DrawCalls++;
            stats.Vertices += count;
            stats.Triangles += count / 3;
        }

        static void DrawArrays(uint32_t count, uint32_t first = 0)
        {
            s_RendererAPI->DrawArrays(count, first);
        }

        static void DrawLines(uint32_t count, uint32_t first = 0)
        {
            s_RendererAPI->DrawLines(count, first);
        }

        static void SetDepthTest(bool enable)
        {
            s_RendererAPI->SetDepthTest(enable);
        }

        static void SetDepthFunc(DepthFunc func)
        {
            s_RendererAPI->SetDepthFunc(func);
        }

        static void SetCullFace(bool enable)
        {
            s_RendererAPI->SetCullFace(enable);
        }

        static void SetCullFaceMode(CullFaceMode mode)
        {
            s_RendererAPI->SetCullFaceMode(mode);
        }

        static void SetLineWidth(float width)
        {
            s_RendererAPI->SetLineWidth(width);
        }

        static void BindTextureUnit(uint32_t slot, uint32_t textureID)
        {
            s_RendererAPI->BindTextureUnit(slot, textureID);
        }

        static void ClearColorOnly()
        {
            s_RendererAPI->ClearColorOnly();
        }

        static int GetBoundFramebufferID()
        {
            return s_RendererAPI->GetBoundFramebufferID();
        }

        static void BindFramebufferByID(int id)
        {
            s_RendererAPI->BindFramebufferByID(id);
        }

    private:
        static Scope<RendererAPI> s_RendererAPI;
    };

} // namespace Engine
