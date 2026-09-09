#pragma once

// 服务器会话管理：会话 → 玩家槽位/可控实体 映射 + 每会话输入状态。
// 玩家槽位按连接顺序分配（0 号会话 → Player1 实体，1 号 → Player2 …）。

#include "Core/Base.h"
#include "Network/Protocol.h"
#include "Scene/SceneEntityIndex.h"

#include <entt/entt.hpp>

#include <array>
#include <string>
#include <unordered_map>

namespace Engine
{

    class Scene;

    struct PlayerSlot
    {
        bool        Occupied   = false;
        uint64_t    SessionId  = 0;
        entt::entity Entity    = entt::null;
        std::string Tag;

        // 输入状态（最近一次 C2S_Input）
        uint16_t    InputBits  = 0;
        uint32_t    LastSeq    = 0;
        bool        HasInput   = false;
    };

    class SessionManager
    {
    public:
        static constexpr size_t kMaxPlayers = 2;

        // 从场景中收集 Tag == PlayerN 的实体作为玩家槽位实体
        void BindPlayerEntities(Ref<Scene> scene);

        // 返回分配的槽位号；满员返回 UINT16_MAX
        uint16_t AssignSlot(uint64_t sessionId);
        void     ReleaseSlot(uint64_t sessionId);
        uint16_t SlotOfSession(uint64_t sessionId) const;

        void     ApplyInput(uint64_t sessionId, uint16_t inputBits, uint32_t seq);
        PlayerSlot* GetSlot(uint16_t index) { return index < kMaxPlayers ? &m_Slots[index] : nullptr; }

    private:
        std::array<PlayerSlot, kMaxPlayers> m_Slots;
        Ref<Scene>                          m_Scene;
    };

} // namespace Engine
