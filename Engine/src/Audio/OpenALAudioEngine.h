#pragma once

#include <cstdint>
#include <glm/glm.hpp>

struct ALCdevice;
struct ALCcontext;

namespace Engine
{

    struct AudioClip;

    class OpenALAudioEngine
    {
    public:
        static OpenALAudioEngine& Get();

        void Init();
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        uint32_t CreateBuffer(const AudioClip& clip);
        void     DestroyBuffer(uint32_t buffer);

        uint32_t CreateSource();
        void     DestroySource(uint32_t source);

        void Play(uint32_t source, uint32_t buffer, bool loop = false);
        void Stop(uint32_t source);
        void Pause(uint32_t source);
        void Resume(uint32_t source);
        bool IsPlaying(uint32_t source) const;

        void SetSourcePosition(uint32_t source, const glm::vec3& pos);
        void SetSourceVolume(uint32_t source, float volume);
        void SetSourcePitch(uint32_t source, float pitch);
        void SetSourceMinDistance(uint32_t source, float dist);
        void SetSourceMaxDistance(uint32_t source, float dist);
        void SetSourceSpatial(uint32_t source, bool spatial);

        void SetListenerPosition(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up);

        // Streaming for video audio (queue buffers)
        void     QueueBuffer(uint32_t source, uint32_t buffer);
        int      GetProcessedBuffers(uint32_t source);
        uint32_t UnqueueBuffer(uint32_t source);

    private:
        OpenALAudioEngine()  = default;
        ~OpenALAudioEngine() = default;

        ALCdevice*  m_Device      = nullptr;
        ALCcontext* m_Context     = nullptr;
        bool        m_Initialized = false;
    };

} // namespace Engine
