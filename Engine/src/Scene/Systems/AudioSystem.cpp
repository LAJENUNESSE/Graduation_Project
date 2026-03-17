#include "engpch.h"
#include "Scene/Systems/AudioSystem.h"
#include "Asset/PathUtils.h"
#include "Audio/AudioClip.h"
#include "Audio/OpenALAudioEngine.h"
#include "Core/Log.h"
#include "Scene/Components.h"

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
            auto&       asc       = view.get<AudioSourceComponent>(entity);
            std::string audioPath = asc.AudioPath;

            if (audioPath.empty())
                continue;

            if (!PathUtils::IsSafeAssetPath(audioPath))
            {
                std::string normalizedPath;
                if (PathUtils::TryToProjectRelative(audioPath, normalizedPath))
                {
                    asc.AudioPath = normalizedPath;
                    audioPath     = normalizedPath;
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

            uint32_t          eid = static_cast<uint32_t>(entity);
            AudioRuntimeState state;
            state.Source = source;
            state.Buffer = buffer;

            if (asc.Spatial && reg.all_of<TransformComponent>(entity))
            {
                auto& tc = reg.get<TransformComponent>(entity);
                audio.SetSourcePosition(source, tc.Translation);
            }

            if (asc.PlayOnStart)
            {
                audio.Play(source, buffer, asc.Loop);
                state.IsPlaying = true;
                ENGINE_CORE_INFO("[AudioSystem] 开始播放音频: {}", audioPath);
            }

            m_Store.Insert(eid, state);
        }
    }

    void AudioSystem::OnRuntimeStop(entt::registry& reg)
    {
        auto& audio = OpenALAudioEngine::Get();

        for (auto& [eid, state] : m_Store)
        {
            if (state.Source != 0)
            {
                audio.Stop(state.Source);
                audio.DestroySource(state.Source);
            }
            if (state.Buffer != 0)
                audio.DestroyBuffer(state.Buffer);
        }
        m_Store.Clear();

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

                auto&     tc      = listenerView.get<TransformComponent>(entity);
                glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 up      = glm::vec3(0.0f, 1.0f, 0.0f);
                audio.SetListenerPosition(tc.Translation, forward, up);
                break; // 只取第一个激活的听众
            }
        }

        // 2. 更新所有音频源的位置和状态
        {
            auto sourceView = reg.view<AudioSourceComponent>();
            for (auto entity : sourceView)
            {
                auto&    asc   = sourceView.get<AudioSourceComponent>(entity);
                uint32_t eid   = static_cast<uint32_t>(entity);
                auto*    state = m_Store.Get(eid);
                if (!state || state->Source == 0)
                    continue;

                // 同步空间位置
                if (asc.Spatial && reg.all_of<TransformComponent>(entity))
                {
                    auto& tc = reg.get<TransformComponent>(entity);
                    audio.SetSourcePosition(state->Source, tc.Translation);
                }

                // 同步音量和音调
                audio.SetSourceVolume(state->Source, asc.Volume);
                audio.SetSourcePitch(state->Source, asc.Pitch);

                // 更新播放状态
                state->IsPlaying = audio.IsPlaying(state->Source);
            }
        }
    }

    void AudioSystem::DestroyEntityAudio(uint32_t entityID)
    {
        auto* state = m_Store.Get(entityID);
        if (!state)
            return;

        auto& audio = OpenALAudioEngine::Get();
        if (state->Source != 0)
        {
            audio.Stop(state->Source);
            audio.DestroySource(state->Source);
        }
        if (state->Buffer != 0)
            audio.DestroyBuffer(state->Buffer);

        m_Store.Remove(entityID);
    }

} // namespace Engine
