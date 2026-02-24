#pragma once

#include "Core/Base.h"
#include "Renderer/Material.h"
#include "Renderer/VertexArray.h"

#include <glm/glm.hpp>
#include <vector>

namespace Engine
{

    struct RenderPacket
    {
        Ref<VertexArray> VAO;
        Ref<Material> Mat;
        glm::mat4 Transform{1.0f};
        int EntityID = -1;
    };

    class RenderQueue
    {
    public:
        void Clear();
        void Submit(const RenderPacket& packet);
        void Flush(const glm::mat4& viewProjection);
        size_t GetPacketCount() const { return m_Packets.size(); }

    private:
        std::vector<RenderPacket> m_Packets;
    };

} // namespace Engine
