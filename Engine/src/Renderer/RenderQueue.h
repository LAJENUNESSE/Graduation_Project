#pragma once

#include "Core/Base.h"
#include "Renderer/Material.h"
#include "Renderer/VertexArray.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <vector>

namespace Engine
{

    struct RenderPacket
    {
        Ref<VertexArray> VAO;
        Ref<Material>    Mat;
        glm::mat4        Transform{1.0f};
        int              EntityID = -1;

        // 排序键：基于 Shader 指针地址，用于按材质/shader 分组以减少状态切换
        uintptr_t SortKey = 0;
    };

    class RenderQueue
    {
    public:
        void   Clear();
        void   Submit(const RenderPacket& packet);
        void   Flush(const glm::mat4& viewProjection);
        size_t GetPacketCount() const { return m_Packets.size(); }

    private:
        std::vector<RenderPacket> m_Packets;
    };

} // namespace Engine
