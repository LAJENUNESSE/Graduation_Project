#pragma once

// 快照/输入编解码：把场景中 Dynamic 刚体的 UUID + Transform 编码为 S2C_Snapshot payload。
// 显式小端逐字段编码（复用 Protocol.h ByteWriter）。Server 与 Editor 共享。

#include "Core/Base.h"
#include "Network/Protocol.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"

namespace Engine::Net
{

    struct SnapshotEntity
    {
        uint64_t  Uuid = 0;
        glm::vec3 Translation{0.0f};
        glm::vec3 Rotation{0.0f}; // 欧拉角
        glm::vec3 Scale{1.0f};
    };

    // 收集场景中所有 Dynamic 刚体的同步状态
    inline std::vector<SnapshotEntity> CollectSnapshotEntities(Ref<Scene>& scene)
    {
        std::vector<SnapshotEntity> entities;
        auto&                       registry = scene->GetRegistry();
        for (auto [entity, id, tc, rb] : registry.view<IDComponent, TransformComponent, RigidBodyComponent>().each())
        {
            if (rb.Type != RigidBodyComponent::BodyType::Dynamic)
                continue;
            SnapshotEntity se;
            se.Uuid        = static_cast<uint64_t>(id.ID);
            se.Translation = tc.Translation;
            se.Rotation    = tc.Rotation;
            se.Scale       = tc.Scale;
            entities.push_back(se);
        }
        return entities;
    }

    // 编码 S2C_Snapshot payload：tick(u32) + count(u16) + N * [uuid(u64) + 9*f32]
    // 9 个 float 依次是 pos3 + rot3 + scale3（旧注释误写为 12*f32，据其实现的解析端会越界）
    inline std::vector<uint8_t> EncodeSnapshot(uint32_t tick, const std::vector<SnapshotEntity>& entities)
    {
        ByteWriter writer;
        writer.U32(tick);
        writer.U16(static_cast<uint16_t>(entities.size()));
        for (const auto& e : entities)
        {
            writer.U64(e.Uuid);
            writer.F32(e.Translation.x);
            writer.F32(e.Translation.y);
            writer.F32(e.Translation.z);
            writer.F32(e.Rotation.x);
            writer.F32(e.Rotation.y);
            writer.F32(e.Rotation.z);
            writer.F32(e.Scale.x);
            writer.F32(e.Scale.y);
            writer.F32(e.Scale.z);
        }
        return writer.Data();
    }

    // 解码（客户端侧）
    struct DecodedSnapshot
    {
        uint32_t                    Tick = 0;
        std::vector<SnapshotEntity> Entities;
    };

    inline bool DecodeSnapshot(const std::vector<uint8_t>& payload, DecodedSnapshot& out)
    {
        ByteReader reader(payload.data(), payload.size());
        if (!reader.U32(out.Tick))
            return false;
        uint16_t count = 0;
        if (!reader.U16(count))
            return false;

        out.Entities.reserve(count);
        for (uint16_t i = 0; i < count; ++i)
        {
            SnapshotEntity e;
            if (!reader.U64(e.Uuid))
                return false;
            if (!reader.F32(e.Translation.x) || !reader.F32(e.Translation.y) || !reader.F32(e.Translation.z))
                return false;
            if (!reader.F32(e.Rotation.x) || !reader.F32(e.Rotation.y) || !reader.F32(e.Rotation.z))
                return false;
            if (!reader.F32(e.Scale.x) || !reader.F32(e.Scale.y) || !reader.F32(e.Scale.z))
                return false;
            out.Entities.push_back(e);
        }
        return true;
    }

    // 编码 C2S_Input payload：seq(u32) + bits(u16)
    inline std::vector<uint8_t> EncodeInput(uint32_t seq, uint16_t bits)
    {
        ByteWriter writer;
        writer.U32(seq);
        writer.U16(bits);
        return writer.Data();
    }

    inline bool DecodeInput(const std::vector<uint8_t>& payload, uint32_t& seq, uint16_t& bits)
    {
        ByteReader reader(payload.data(), payload.size());
        return reader.U32(seq) && reader.U16(bits);
    }

} // namespace Engine::Net
