#include "engpch.h"
#include "Scene/Systems/VideoSystem.h"
#include "Scene/Components.h"
#include "Media/FFmpegDecoder.h"
#include "Audio/OpenALAudioEngine.h"
#include "Audio/AudioClip.h"
#include "Renderer/Texture.h"
#include "Core/Base.h"
#include "Core/Log.h"

#include <cstring>

namespace Engine
{

    // AL_FORMAT_STEREO16 = 0x1103
    static constexpr uint32_t kALFormatStereo16 = 0x1103;
    static constexpr int kStreamingBufferCount = 4;

    void VideoSystem::Init()
    {
        // OpenAL 由 AudioSystem 负责初始化，此处无需操作
    }

    void VideoSystem::Shutdown()
    {
        // 资源清理在 OnRuntimeStop 中完成
    }

    void VideoSystem::OnRuntimeStart(entt::registry& reg)
    {
        auto& audio = OpenALAudioEngine::Get();

        auto view = reg.view<VideoPlayerComponent>();
        for (auto entity : view)
        {
            auto& vp = view.get<VideoPlayerComponent>(entity);

            if (vp.StreamURL.empty())
                continue;

            // 创建解码器
            FFmpegDecoder* decoder = new FFmpegDecoder();
            if (!decoder->Open(vp.StreamURL))
            {
                ENGINE_CORE_ERROR("[VideoSystem] 无法打开视频流: {}", vp.StreamURL);
                delete decoder;
                continue;
            }

            vp.RuntimeDecoder = decoder;

            // 视频流：创建纹理
            if (decoder->HasVideoStream())
            {
                int w = decoder->GetVideoWidth();
                int h = decoder->GetVideoHeight();
                if (w > 0 && h > 0)
                {
                    vp.RuntimeTexture = Texture2D::Create(static_cast<uint32_t>(w),
                                                          static_cast<uint32_t>(h));
                    ENGINE_CORE_INFO("[VideoSystem] 创建视频纹理 {}x{}", w, h);
                }
            }

            // 音频流：创建源和流式缓冲区
            if (decoder->HasAudioStream())
            {
                vp.RuntimeAudioSource = audio.CreateSource();
                audio.SetSourceVolume(vp.RuntimeAudioSource, vp.Volume);
                audio.SetSourceSpatial(vp.RuntimeAudioSource, false); // 视频音频不做空间化

                // 创建流式音频缓冲区
                vp.RuntimeAudioBuffers.resize(kStreamingBufferCount);
                for (int i = 0; i < kStreamingBufferCount; i++)
                {
                    // 创建一个空的初始缓冲区
                    AudioClip emptyClip;
                    emptyClip.SampleRate = static_cast<uint32_t>(decoder->GetAudioSampleRate());
                    emptyClip.Channels = 2;
                    emptyClip.BitsPerSample = 16;
                    emptyClip.ALFormat = kALFormatStereo16;
                    // 最小可用的静音缓冲区（1个采样点 × 2通道 × 2字节）
                    emptyClip.Data.resize(4, 0);
                    vp.RuntimeAudioBuffers[i] = audio.CreateBuffer(emptyClip);
                }

                // 先将所有缓冲区入队并开始播放
                for (int i = 0; i < kStreamingBufferCount; i++)
                {
                    audio.QueueBuffer(vp.RuntimeAudioSource, vp.RuntimeAudioBuffers[i]);
                }
                audio.Resume(vp.RuntimeAudioSource);
            }

            vp.IsPlaying = true;
            ENGINE_CORE_INFO("[VideoSystem] 视频流已启动: {}", vp.StreamURL);
        }
    }

    void VideoSystem::OnRuntimeStop(entt::registry& reg)
    {
        auto& audio = OpenALAudioEngine::Get();

        auto view = reg.view<VideoPlayerComponent>();
        for (auto entity : view)
        {
            auto& vp = view.get<VideoPlayerComponent>(entity);

            // 关闭并销毁解码器
            if (vp.RuntimeDecoder)
            {
                vp.RuntimeDecoder->Close();
                delete vp.RuntimeDecoder;
                vp.RuntimeDecoder = nullptr;
            }

            // 销毁音频源（必须先停止并反入队所有缓冲区）
            if (vp.RuntimeAudioSource != 0)
            {
                audio.Stop(vp.RuntimeAudioSource);
                // 反入队所有已处理的缓冲区
                int processed = audio.GetProcessedBuffers(vp.RuntimeAudioSource);
                while (processed > 0)
                {
                    audio.UnqueueBuffer(vp.RuntimeAudioSource);
                    processed--;
                }
                audio.DestroySource(vp.RuntimeAudioSource);
                vp.RuntimeAudioSource = 0;
            }

            // 销毁音频缓冲区
            for (uint32_t buf : vp.RuntimeAudioBuffers)
            {
                if (buf != 0)
                    audio.DestroyBuffer(buf);
            }
            vp.RuntimeAudioBuffers.clear();

            // 释放纹理（Ref 自动清理）
            vp.RuntimeTexture = nullptr;

            vp.IsPlaying = false;
        }

        ENGINE_CORE_INFO("[VideoSystem] 运行时视频资源已清理");
    }

    void VideoSystem::OnUpdate(entt::registry& reg, float dt)
    {
        auto& audio = OpenALAudioEngine::Get();

        auto view = reg.view<VideoPlayerComponent>();
        for (auto entity : view)
        {
            auto& vp = view.get<VideoPlayerComponent>(entity);

            if (!vp.RuntimeDecoder || !vp.IsPlaying)
                continue;

            // === 视频帧更新 ===
            if (vp.RuntimeDecoder->HasNewVideoFrame() && vp.RuntimeTexture)
            {
                const uint8_t* frameData = vp.RuntimeDecoder->GetVideoFrameRGBA();
                if (frameData)
                {
                    int w = vp.RuntimeDecoder->GetVideoWidth();
                    int h = vp.RuntimeDecoder->GetVideoHeight();
                    uint32_t dataSize = static_cast<uint32_t>(w * h * 4);
                    vp.RuntimeTexture->SetData((void*)frameData, dataSize);
                }
            }

            // === 音频流更新 ===
            if (vp.RuntimeDecoder->HasAudioStream() && vp.RuntimeAudioSource != 0)
            {
                // 同步音量
                audio.SetSourceVolume(vp.RuntimeAudioSource, vp.Volume);

                // 处理已播放完的缓冲区：反入队 → 填充新数据 → 重新入队
                int processed = audio.GetProcessedBuffers(vp.RuntimeAudioSource);
                while (processed > 0)
                {
                    uint32_t buf = audio.UnqueueBuffer(vp.RuntimeAudioSource);

                    int sampleCount = 0;
                    const int16_t* audioData = vp.RuntimeDecoder->GetAudioData(sampleCount);
                    if (audioData && sampleCount > 0)
                    {
                        // 用新的音频数据重新填充缓冲区
                        AudioClip clip;
                        clip.SampleRate = static_cast<uint32_t>(vp.RuntimeDecoder->GetAudioSampleRate());
                        clip.Channels = 2; // 立体声（FFmpeg SwrContext 会重采样为立体声）
                        clip.BitsPerSample = 16;
                        clip.ALFormat = kALFormatStereo16;
                        size_t byteSize = static_cast<size_t>(sampleCount) * 2 * sizeof(int16_t);
                        clip.Data.resize(byteSize);
                        std::memcpy(clip.Data.data(), audioData, byteSize);

                        // 销毁旧缓冲区，创建新的并入队
                        audio.DestroyBuffer(buf);
                        buf = audio.CreateBuffer(clip);
                    }

                    audio.QueueBuffer(vp.RuntimeAudioSource, buf);
                    processed--;
                }

                // 检查是否因数据耗尽而停止（buffer underrun），重新启动
                if (!audio.IsPlaying(vp.RuntimeAudioSource))
                {
                    audio.Resume(vp.RuntimeAudioSource);
                }
            }

            // 检查解码器是否仍在运行（流是否结束）
            if (!vp.RuntimeDecoder->IsOpen())
            {
                if (vp.Loop)
                {
                    // 循环模式：重新打开流
                    vp.RuntimeDecoder->Close();
                    if (vp.RuntimeDecoder->Open(vp.StreamURL))
                    {
                        ENGINE_CORE_INFO("[VideoSystem] 循环重新打开视频流: {}", vp.StreamURL);
                    }
                    else
                    {
                        ENGINE_CORE_WARN("[VideoSystem] 无法重新打开视频流: {}", vp.StreamURL);
                        vp.IsPlaying = false;
                    }
                }
                else
                {
                    vp.IsPlaying = false;
                    ENGINE_CORE_INFO("[VideoSystem] 视频流播放完毕: {}", vp.StreamURL);
                }
            }
        }
    }

} // namespace Engine
