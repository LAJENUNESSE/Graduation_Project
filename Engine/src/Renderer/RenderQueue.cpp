#include "engpch.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/RenderCommand.h"

namespace Engine
{

    void RenderQueue::Clear()
    {
        m_Packets.clear();
    }

    void RenderQueue::Submit(const RenderPacket& packet)
    {
        m_Packets.push_back(packet);
        // 计算排序键：使用 Shader 裸指针地址，相同 shader 的 packet 会被分组到一起
        auto& p = m_Packets.back();
        if (p.Mat && p.Mat->GetShader())
            p.SortKey = reinterpret_cast<uintptr_t>(p.Mat->GetShader().get());
    }

    void RenderQueue::Flush(const glm::mat4& viewProjection)
    {
        // 按 SortKey 排序，使相同 shader/材质的绘制调用相邻，减少 GPU 状态切换
        std::sort(m_Packets.begin(), m_Packets.end(),
                  [](const RenderPacket& a, const RenderPacket& b) { return a.SortKey < b.SortKey; });

        // 跟踪上一个绑定的材质，相同材质跳过重复 Bind
        Material* lastBoundMaterial = nullptr;

        for (const auto& packet : m_Packets)
        {
            if (packet.Mat)
            {
                Material* currentMat = packet.Mat.get();
                if (currentMat != lastBoundMaterial)
                {
                    packet.Mat->Bind();
                    lastBoundMaterial = currentMat;
                }

                // 逐绘制调用的 uniform：ViewProjection 和 Transform（每次都需要设置）
                const auto& shader = packet.Mat->GetShader();
                shader->SetMat4("u_ViewProjection", viewProjection);
                shader->SetMat4("u_Transform", packet.Transform);
                shader->SetMat3("u_NormalMatrix", glm::transpose(glm::inverse(glm::mat3(packet.Transform))));
            }

            packet.VAO->Bind();
            RenderCommand::DrawIndexed(packet.VAO);
        }
    }

} // namespace Engine
