#include "engpch.h"
#include "Audio/OpenALAudioEngine.h"
#include "Audio/AudioClip.h"
#include "Core/Log.h"

#include <AL/al.h>
#include <AL/alc.h>

namespace Engine
{

    // ---- helper: check and log AL errors ----
    static bool CheckALError(const char* context)
    {
        ALenum err = alGetError();
        if (err != AL_NO_ERROR)
        {
            ENGINE_CORE_ERROR("OpenAL 错误 [{}]: 0x{:04X}", context, (unsigned)err);
            return true;
        }
        return false;
    }

    OpenALAudioEngine& OpenALAudioEngine::Get()
    {
        static OpenALAudioEngine instance;
        return instance;
    }

    void OpenALAudioEngine::Init()
    {
        if (m_Initialized)
            return;

        // Open default device
        m_Device = alcOpenDevice(nullptr);
        if (!m_Device)
        {
            ENGINE_CORE_ERROR("OpenALAudioEngine: 无法打开音频设备");
            return;
        }

        const char* deviceName = alcGetString(m_Device, ALC_DEVICE_SPECIFIER);
        ENGINE_CORE_INFO("OpenALAudioEngine: 音频设备 '{}'", deviceName ? deviceName : "unknown");

        // Create context
        m_Context = alcCreateContext(m_Device, nullptr);
        if (!m_Context)
        {
            ENGINE_CORE_ERROR("OpenALAudioEngine: 无法创建音频上下文");
            alcCloseDevice(m_Device);
            m_Device = nullptr;
            return;
        }

        if (!alcMakeContextCurrent(m_Context))
        {
            ENGINE_CORE_ERROR("OpenALAudioEngine: 无法激活音频上下文");
            alcDestroyContext(m_Context);
            alcCloseDevice(m_Device);
            m_Context = nullptr;
            m_Device = nullptr;
            return;
        }

        // Use inverse distance clamped attenuation model
        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);

        m_Initialized = true;
        ENGINE_CORE_INFO("OpenALAudioEngine: 初始化完成");
    }

    void OpenALAudioEngine::Shutdown()
    {
        if (!m_Initialized)
            return;

        alcMakeContextCurrent(nullptr);

        if (m_Context)
        {
            alcDestroyContext(m_Context);
            m_Context = nullptr;
        }

        if (m_Device)
        {
            alcCloseDevice(m_Device);
            m_Device = nullptr;
        }

        m_Initialized = false;
        ENGINE_CORE_INFO("OpenALAudioEngine: 已关闭");
    }

    // ---- Buffer management ----

    uint32_t OpenALAudioEngine::CreateBuffer(const AudioClip& clip)
    {
        if (!m_Initialized || !clip.IsValid())
            return 0;

        ALuint buf = 0;
        alGenBuffers(1, &buf);
        if (CheckALError("alGenBuffers"))
            return 0;

        alBufferData(buf, clip.ALFormat, clip.Data.data(),
                     static_cast<ALsizei>(clip.Data.size()),
                     static_cast<ALsizei>(clip.SampleRate));
        if (CheckALError("alBufferData"))
        {
            alDeleteBuffers(1, &buf);
            return 0;
        }

        return static_cast<uint32_t>(buf);
    }

    void OpenALAudioEngine::DestroyBuffer(uint32_t buffer)
    {
        if (!m_Initialized || buffer == 0)
            return;

        ALuint buf = static_cast<ALuint>(buffer);
        alDeleteBuffers(1, &buf);
        CheckALError("alDeleteBuffers");
    }

    // ---- Source management ----

    uint32_t OpenALAudioEngine::CreateSource()
    {
        if (!m_Initialized)
            return 0;

        ALuint src = 0;
        alGenSources(1, &src);
        if (CheckALError("alGenSources"))
            return 0;

        // Sensible defaults
        alSourcef(src, AL_GAIN, 1.0f);
        alSourcef(src, AL_PITCH, 1.0f);
        alSourcef(src, AL_REFERENCE_DISTANCE, 1.0f);
        alSourcef(src, AL_MAX_DISTANCE, 100.0f);
        alSourcei(src, AL_LOOPING, AL_FALSE);

        return static_cast<uint32_t>(src);
    }

    void OpenALAudioEngine::DestroySource(uint32_t source)
    {
        if (!m_Initialized || source == 0)
            return;

        ALuint src = static_cast<ALuint>(source);
        alSourceStop(src); // stop before deleting
        alDeleteSources(1, &src);
        CheckALError("alDeleteSources");
    }

    // ---- Playback control ----

    void OpenALAudioEngine::Play(uint32_t source, uint32_t buffer, bool loop)
    {
        if (!m_Initialized || source == 0)
            return;

        ALuint src = static_cast<ALuint>(source);
        alSourcei(src, AL_BUFFER, static_cast<ALint>(buffer));
        alSourcei(src, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
        alSourcePlay(src);
        CheckALError("Play");
    }

    void OpenALAudioEngine::Stop(uint32_t source)
    {
        if (!m_Initialized || source == 0)
            return;

        alSourceStop(static_cast<ALuint>(source));
    }

    void OpenALAudioEngine::Pause(uint32_t source)
    {
        if (!m_Initialized || source == 0)
            return;

        alSourcePause(static_cast<ALuint>(source));
    }

    void OpenALAudioEngine::Resume(uint32_t source)
    {
        if (!m_Initialized || source == 0)
            return;

        alSourcePlay(static_cast<ALuint>(source));
    }

    bool OpenALAudioEngine::IsPlaying(uint32_t source) const
    {
        if (!m_Initialized || source == 0)
            return false;

        ALint state = 0;
        alGetSourcei(static_cast<ALuint>(source), AL_SOURCE_STATE, &state);
        return state == AL_PLAYING;
    }

    // ---- Source properties ----

    void OpenALAudioEngine::SetSourcePosition(uint32_t source, const glm::vec3& pos)
    {
        if (!m_Initialized || source == 0)
            return;

        alSource3f(static_cast<ALuint>(source), AL_POSITION, pos.x, pos.y, pos.z);
    }

    void OpenALAudioEngine::SetSourceVolume(uint32_t source, float volume)
    {
        if (!m_Initialized || source == 0)
            return;

        alSourcef(static_cast<ALuint>(source), AL_GAIN, volume);
    }

    void OpenALAudioEngine::SetSourcePitch(uint32_t source, float pitch)
    {
        if (!m_Initialized || source == 0)
            return;

        alSourcef(static_cast<ALuint>(source), AL_PITCH, pitch);
    }

    void OpenALAudioEngine::SetSourceMinDistance(uint32_t source, float dist)
    {
        if (!m_Initialized || source == 0)
            return;

        alSourcef(static_cast<ALuint>(source), AL_REFERENCE_DISTANCE, dist);
    }

    void OpenALAudioEngine::SetSourceMaxDistance(uint32_t source, float dist)
    {
        if (!m_Initialized || source == 0)
            return;

        alSourcef(static_cast<ALuint>(source), AL_MAX_DISTANCE, dist);
    }

    void OpenALAudioEngine::SetSourceSpatial(uint32_t source, bool spatial)
    {
        if (!m_Initialized || source == 0)
            return;

        ALuint src = static_cast<ALuint>(source);
        if (spatial)
        {
            // World-space positioning
            alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE);
        }
        else
        {
            // Non-spatial: relative to listener at origin (always "here")
            alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
            alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
        }
    }

    // ---- Listener ----

    void OpenALAudioEngine::SetListenerPosition(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up)
    {
        if (!m_Initialized)
            return;

        alListener3f(AL_POSITION, pos.x, pos.y, pos.z);

        float ori[6] = { forward.x, forward.y, forward.z, up.x, up.y, up.z };
        alListenerfv(AL_ORIENTATION, ori);
    }

    // ---- Streaming helpers ----

    void OpenALAudioEngine::QueueBuffer(uint32_t source, uint32_t buffer)
    {
        if (!m_Initialized || source == 0 || buffer == 0)
            return;

        ALuint buf = static_cast<ALuint>(buffer);
        alSourceQueueBuffers(static_cast<ALuint>(source), 1, &buf);
        CheckALError("alSourceQueueBuffers");
    }

    int OpenALAudioEngine::GetProcessedBuffers(uint32_t source)
    {
        if (!m_Initialized || source == 0)
            return 0;

        ALint count = 0;
        alGetSourcei(static_cast<ALuint>(source), AL_BUFFERS_PROCESSED, &count);
        return static_cast<int>(count);
    }

    uint32_t OpenALAudioEngine::UnqueueBuffer(uint32_t source)
    {
        if (!m_Initialized || source == 0)
            return 0;

        ALuint buf = 0;
        alSourceUnqueueBuffers(static_cast<ALuint>(source), 1, &buf);
        if (CheckALError("alSourceUnqueueBuffers"))
            return 0;

        return static_cast<uint32_t>(buf);
    }

} // namespace Engine
