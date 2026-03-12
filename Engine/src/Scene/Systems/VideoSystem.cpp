#include "engpch.h"
#include "Scene/Systems/VideoSystem.h"
#include "Audio/AudioClip.h"
#include "Audio/OpenALAudioEngine.h"
#include "Core/Base.h"
#include "Core/Log.h"
#include "Media/FFmpegDecoder.h"
#include "Renderer/Texture.h"
#include "Scene/Components.h"

#include <cstring>

namespace Engine
{

    // VideoRuntimeState 特殊成员函数（需要 FFmpegDecoder 完整类型）
    VideoRuntimeState::VideoRuntimeState() = default;
    VideoRuntimeState::~VideoRuntimeState()
    {
        if (OpenThread.joinable())
            OpenThread.join();
    }
    VideoRuntimeState::VideoRuntimeState(VideoRuntimeState&&) noexcept = default;
    VideoRuntimeState& VideoRuntimeState::operator=(VideoRuntimeState&&) noexcept = default;

    // AL_FORMAT_STEREO16 = 0x1103
    static constexpr uint32_t kALFormatStereo16 = 0x1103;
    static constexpr int kStreamingBufferCount = 4;

    void VideoSystem::Init()
    {
        // OpenAL 由 AudioSystem 负责初始化，此处无需操作
    }

    void VideoSystem::Shutdown()
    {
        m_Store.Clear();
    }

    void VideoSystem::OnRuntimeStart(entt::registry& reg)
    {
        auto view = reg.view<VideoPlayerComponent>();
        for (auto entity : view)
        {
            auto& vp = view.get<VideoPlayerComponent>(entity);

            if (vp.StreamURL.empty())
                continue;

            uint32_t eid = static_cast<uint32_t>(entity);
            VideoRuntimeState state;
            state.Decoder = std::make_unique<FFmpegDecoder>();
            state.IsPlaying = false;

            // 后台线程打开流，避免阻塞主线程
            std::string url = vp.StreamURL;
            FFmpegDecoder* rawDecoder = state.Decoder.get();
            state.OpenThread = std::thread(
                [rawDecoder, url]()
                {
                    if (!rawDecoder->Open(url))
                    {
                        ENGINE_CORE_ERROR("[VideoSystem] 无法打开视频流: {}", url);
                    }
                });

            ENGINE_CORE_INFO("[VideoSystem] 正在后台连接视频流: {}", vp.StreamURL);
            m_Store.Insert(eid, std::move(state));
        }
    }

    void VideoSystem::OnRuntimeStop(entt::registry& reg)
    {
        auto& audio = OpenALAudioEngine::Get();

        for (auto& [eid, state] : m_Store)
        {
            // join 后台打开线程
            if (state.OpenThread.joinable())
                state.OpenThread.join();

            // 关闭并销毁解码器
            if (state.Decoder)
            {
                state.Decoder->Close();
                state.Decoder.reset();
            }

            // 销毁音频源
            if (state.AudioSource != 0)
            {
                audio.Stop(state.AudioSource);
                int processed = audio.GetProcessedBuffers(state.AudioSource);
                while (processed > 0)
                {
                    audio.UnqueueBuffer(state.AudioSource);
                    processed--;
                }
                audio.DestroySource(state.AudioSource);
            }

            // 销毁音频缓冲区
            for (uint32_t buf : state.AudioBuffers)
            {
                if (buf != 0)
                    audio.DestroyBuffer(buf);
            }
        }
        m_Store.Clear();

        ENGINE_CORE_INFO("[VideoSystem] 运行时视频资源已清理");
    }

    void VideoSystem::OnUpdate(entt::registry& reg, float dt)
    {
        auto& audio = OpenALAudioEngine::Get();

        auto view = reg.view<VideoPlayerComponent>();
        for (auto entity : view)
        {
            auto& vp = view.get<VideoPlayerComponent>(entity);
            uint32_t eid = static_cast<uint32_t>(entity);
            auto* state = m_Store.Get(eid);

            if (!state || !state->Decoder)
                continue;

            // === 延迟初始化：后台 Open() 完成后在主线程创建 GPU 资源 ===
            if (!state->IsPlaying && state->Decoder->IsOpen())
            {
                // 视频纹理（必须在主线程创建 OpenGL 对象）
                if (state->Decoder->HasVideoStream())
                {
                    int w = state->Decoder->GetVideoWidth();
                    int h = state->Decoder->GetVideoHeight();
                    if (w > 0 && h > 0)
                    {
                        state->Texture = Texture2D::Create(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                        ENGINE_CORE_INFO("[VideoSystem] 创建视频纹理 {}x{}", w, h);
                    }
                }

                // 音频流
                if (state->Decoder->HasAudioStream())
                {
                    state->AudioSource = audio.CreateSource();
                    audio.SetSourceVolume(state->AudioSource, vp.Volume);
                    audio.SetSourceSpatial(state->AudioSource, false);

                    state->AudioBuffers.resize(kStreamingBufferCount);
                    for (int i = 0; i < kStreamingBufferCount; i++)
                    {
                        AudioClip emptyClip;
                        emptyClip.SampleRate = static_cast<uint32_t>(state->Decoder->GetAudioSampleRate());
                        emptyClip.Channels = 2;
                        emptyClip.BitsPerSample = 16;
                        emptyClip.ALFormat = kALFormatStereo16;
                        emptyClip.Data.resize(4, 0);
                        state->AudioBuffers[i] = audio.CreateBuffer(emptyClip);
                    }
                    for (int i = 0; i < kStreamingBufferCount; i++)
                        audio.QueueBuffer(state->AudioSource, state->AudioBuffers[i]);
                    audio.Resume(state->AudioSource);
                }

                state->IsPlaying = true;
                ENGINE_CORE_INFO("[VideoSystem] 视频流已就绪: 视频={}x{}", state->Decoder->GetVideoWidth(),
                                 state->Decoder->GetVideoHeight());
            }

            if (!state->IsPlaying)
                continue;

            // === 视频帧更新 ===
            if (state->Decoder->HasNewVideoFrame() && state->Texture)
            {
                const uint8_t* frameData = state->Decoder->GetVideoFrameRGBA();
                if (frameData)
                {
                    int w = state->Decoder->GetVideoWidth();
                    int h = state->Decoder->GetVideoHeight();
                    uint32_t dataSize = static_cast<uint32_t>(w * h * 4);
                    state->Texture->SetData((void*)frameData, dataSize);
                }
            }

            // === 音频流更新 ===
            if (state->Decoder->HasAudioStream() && state->AudioSource != 0)
            {
                // 同步音量
                audio.SetSourceVolume(state->AudioSource, vp.Volume);

                // 处理已播放完的缓冲区
                int processed = audio.GetProcessedBuffers(state->AudioSource);
                while (processed > 0)
                {
                    uint32_t buf = audio.UnqueueBuffer(state->AudioSource);

                    int sampleCount = 0;
                    const int16_t* audioData = state->Decoder->GetAudioData(sampleCount);
                    if (audioData && sampleCount > 0)
                    {
                        AudioClip clip;
                        clip.SampleRate = static_cast<uint32_t>(state->Decoder->GetAudioSampleRate());
                        clip.Channels = 2;
                        clip.BitsPerSample = 16;
                        clip.ALFormat = kALFormatStereo16;
                        size_t byteSize = static_cast<size_t>(sampleCount) * 2 * sizeof(int16_t);
                        clip.Data.resize(byteSize);
                        std::memcpy(clip.Data.data(), audioData, byteSize);

                        audio.DestroyBuffer(buf);
                        buf = audio.CreateBuffer(clip);
                    }

                    audio.QueueBuffer(state->AudioSource, buf);
                    processed--;
                }

                // 检查是否因数据耗尽而停止
                if (!audio.IsPlaying(state->AudioSource))
                {
                    audio.Resume(state->AudioSource);
                }
            }

            // 检查解码器是否仍在运行
            if (!state->Decoder->IsOpen())
            {
                if (vp.Loop)
                {
                    state->Decoder->Close();
                    if (state->Decoder->Open(vp.StreamURL))
                    {
                        ENGINE_CORE_INFO("[VideoSystem] 循环重新打开视频流: {}", vp.StreamURL);
                    }
                    else
                    {
                        ENGINE_CORE_WARN("[VideoSystem] 无法重新打开视频流: {}", vp.StreamURL);
                        state->IsPlaying = false;
                    }
                }
                else
                {
                    state->IsPlaying = false;
                    ENGINE_CORE_INFO("[VideoSystem] 视频流播放完毕: {}", vp.StreamURL);
                }
            }
        }
    }

    void VideoSystem::DestroyEntityVideo(uint32_t entityID)
    {
        auto* state = m_Store.Get(entityID);
        if (!state)
            return;

        auto& audio = OpenALAudioEngine::Get();

        // join 后台线程
        if (state->OpenThread.joinable())
            state->OpenThread.join();

        if (state->Decoder)
        {
            state->Decoder->Close();
            state->Decoder.reset();
        }

        if (state->AudioSource != 0)
        {
            audio.Stop(state->AudioSource);
            int processed = audio.GetProcessedBuffers(state->AudioSource);
            while (processed > 0)
            {
                audio.UnqueueBuffer(state->AudioSource);
                processed--;
            }
            audio.DestroySource(state->AudioSource);
        }

        for (uint32_t buf : state->AudioBuffers)
        {
            if (buf != 0)
                audio.DestroyBuffer(buf);
        }

        m_Store.Remove(entityID);
    }

} // namespace Engine
