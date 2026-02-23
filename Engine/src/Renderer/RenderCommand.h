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

    private:
        static Scope<RendererAPI> s_RendererAPI;
    };

} // namespace Engine
