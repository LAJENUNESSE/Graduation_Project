#pragma once

#include "Core/Base.h"
#include "Renderer/Texture.h"

#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Engine
{

    class FFmpegDecoder;

    struct VideoRuntimeState
    {
        std::unique_ptr<FFmpegDecoder> Decoder;
        uint32_t                       AudioSource = 0;
        Ref<Texture2D>                 Texture;
        std::vector<uint32_t>          AudioBuffers;
        std::thread                    OpenThread;
        bool                           IsPlaying = false;

        VideoRuntimeState();
        ~VideoRuntimeState();
        VideoRuntimeState(VideoRuntimeState&&) noexcept;
        VideoRuntimeState& operator=(VideoRuntimeState&&) noexcept;

        VideoRuntimeState(const VideoRuntimeState&)            = delete;
        VideoRuntimeState& operator=(const VideoRuntimeState&) = delete;
    };

    class VideoRuntimeStore
    {
    public:
        void Insert(uint32_t entityID, VideoRuntimeState state) { m_States[entityID] = std::move(state); }

        void Remove(uint32_t entityID) { m_States.erase(entityID); }

        VideoRuntimeState* Get(uint32_t entityID)
        {
            auto it = m_States.find(entityID);
            return it != m_States.end() ? &it->second : nullptr;
        }

        const VideoRuntimeState* Get(uint32_t entityID) const
        {
            auto it = m_States.find(entityID);
            return it != m_States.end() ? &it->second : nullptr;
        }

        void Clear() { m_States.clear(); }

        auto begin() { return m_States.begin(); }
        auto end() { return m_States.end(); }

    private:
        std::unordered_map<uint32_t, VideoRuntimeState> m_States;
    };

} // namespace Engine
