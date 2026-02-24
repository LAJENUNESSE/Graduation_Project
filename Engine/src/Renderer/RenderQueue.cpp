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
    }

    void RenderQueue::Flush(const glm::mat4& viewProjection)
    {
        for (const auto& packet : m_Packets)
        {
            if (packet.Mat)
            {
                packet.Mat->Bind();
                // Per-draw uniforms: ViewProjection and Transform
                auto shader = packet.Mat->GetShader();
                shader->SetMat4("u_ViewProjection", viewProjection);
                shader->SetMat4("u_Transform", packet.Transform);
            }

            packet.VAO->Bind();
            RenderCommand::DrawIndexed(packet.VAO);
        }
    }

} // namespace Engine
