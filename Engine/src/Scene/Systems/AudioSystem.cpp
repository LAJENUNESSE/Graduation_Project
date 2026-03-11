#include "Scene/Systems/AudioSystem.h"
#include "Asset/PathUtils.h"
#include "Audio/AudioClip.h"
#include "Audio/OpenALAudioEngine.h"
#include "Core/Log.h"
#include "Scene/Components.h"
#include "engpch.h"

namespace Engine
{

    void AudioSystem::Init()
    {
        OpenALAudioEngine::Get().Init();
        ENGINE_CORE_INFO("[AudioSystem] OpenAL 音频引擎已初始化");
    }

    void AudioSystem::Shutdown()
    {
        OpenALAudioEngine::Get().Shutdown();
        ENGINE_CORE_INFO("[AudioSystem] OpenAL 音频引擎已关闭");
    }

    void AudioSystem::OnRuntimeStart(entt::registry& reg)
    {
        auto& audio = OpenALAudioEngine::Get();

        auto view = reg.view<AudioSourceComponent>();
        for (auto entity : view)
        {
            auto& asc = view.get<AudioSourceComponent>(entity);
            std::string audioPath = asc.AudioPath;

            if (audioPath.empty())
                continue;

            if (!PathUtils::IsSafeAssetPath(audioPath))
            {
                std::string normalizedPath;
                if (PathUtils::TryToProjectRelative(audioPath, normalizedPath))
                {
                    asc.AudioPath = normalizedPath;
                    audioPath = normalizedPath;
                }
                else
                {
                    ENGINE_CORE_WARN("[AudioSystem] 不安全的音频路径: {}", audioPath);
                    continue;
                }
            }

            AudioClip clip = AudioClip::LoadFromFile(audioPath);
            if (!clip.IsValid())
            {
                ENGINE_CORE_WARN("[AudioSystem] 无法加载音频文件: {}", audioPath);
                continue;
            }

            uint32_t buffer = audio.CreateBuffer(clip);
            if (buffer == 0)
            {
                ENGINE_CORE_WARN("[AudioSystem] 创建音频缓冲区失败: {}", audioPath);
                continue;
            }

            uint32_t source = audio.CreateSource();
            if (source == 0)
            {
                ENGINE_CORE_WARN("[AudioSystem] 创建音频源失败: {}", audioPath);
                audio.DestroyBuffer(buffer);
                continue;
            }

            audio.SetSourceVolume(source, asc.Volume);
            audio.SetSourcePitch(source, asc.Pitch);
            audio.SetSourceMinDistance(source, asc.MinDistance);
            audio.SetSourceMaxDistance(source, asc.MaxDistance);
            audio.SetSourceSpatial(source, asc.Spatial);

            asc.RuntimeBuffer = buffer;
            asc.RuntimeSource = source;

            if (asc.Spatial && reg.all_of<TransformComponent>(entity))
            {
                auto& tc = reg.get<TransformComponent>(entity);
                audio.SetSourcePosition(source, tc.Translation);
            }

            if (asc.PlayOnStart)
            {
                audio.Play(source, buffer, asc.Loop);
                asc.IsPlaying = true;
                ENGINE_CORE_INFO("[AudioSystem] 开始播放音频: {}", audioPath);
            }
        }
    }

    void AudioSystem::OnRuntimeStop(entt::registry& reg)
    {
        auto& audio = OpenALAudioEngine::Get();

        auto view = reg.view<AudioSourceComponent>();
        for (auto entity : view)
        {
            auto& asc = view.get<AudioSourceComponent>(entity);

            if (asc.RuntimeSource != 0)
            {
                audio.Stop(asc.RuntimeSource);
                audio.DestroySource(asc.RuntimeSource);
                asc.RuntimeSource = 0;
            }

            if (asc.RuntimeBuffer != 0)
            {
                audio.DestroyBuffer(asc.RuntimeBuffer);
                asc.RuntimeBuffer = 0;
            }

            asc.IsPlaying = false;
        }

        ENGINE_CORE_INFO("[AudioSystem] 运行时音频资源已清理");
    }

    void AudioSystem::OnUpdate(entt::registry& reg, float dt)
    {
        auto& audio = OpenALAudioEngine::Get();

        // 1. 更新听众位置（取第一个激活的 AudioListenerComponent）
        {
            auto listenerView = reg.view<AudioListenerComponent, TransformComponent>();
            for (auto entity : listenerView)
            {
                auto& alc = listenerView.get<AudioListenerComponent>(entity);
                if (!alc.Active)
                    continue;

                auto& tc = listenerView.get<TransformComponent>(entity);
                glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
                audio.SetListenerPosition(tc.Translation, forward, up);
                break; // 只取第一个激活的听众
            }
        }

        // 2. 更新所有音频源的位置和状态
        {
            auto sourceView = reg.view<AudioSourceComponent>();
            for (auto entity : sourceView)
            {
                auto& asc = sourceView.get<AudioSourceComponent>(entity);

                if (asc.RuntimeSource == 0)
                    continue;

                // 同步空间位置
                if (asc.Spatial && reg.all_of<TransformComponent>(entity))
                {
                    auto& tc = reg.get<TransformComponent>(entity);
                    audio.SetSourcePosition(asc.RuntimeSource, tc.Translation);
                }

                // 同步音量和音调
                audio.SetSourceVolume(asc.RuntimeSource, asc.Volume);
                audio.SetSourcePitch(asc.RuntimeSource, asc.Pitch);

                // 更新播放状态
                asc.IsPlaying = audio.IsPlaying(asc.RuntimeSource);
            }
        }
    }

} // namespace Engine
