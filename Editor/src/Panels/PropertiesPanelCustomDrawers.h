#pragma once

#include "Scene/Components.h"
#include "Script/NativeScriptComponent.h"

namespace Engine
{
    struct AudioRuntimeState;
    struct VideoRuntimeState;

    namespace PropertiesPanelCustomDrawers
    {
        void DrawMeshRendererInspector(MeshRendererComponent& component);
        void DrawTerrainInspector(TerrainComponent& component);
        void DrawParticleEmitterInspector(ParticleEmitterComponent& component);
        void DrawAudioSourceInspector(AudioSourceComponent& component, const AudioRuntimeState* runtimeState = nullptr);
        void DrawAudioListenerInspector(AudioListenerComponent& component);
        void DrawVideoPlayerInspector(VideoPlayerComponent& component, const VideoRuntimeState* runtimeState = nullptr);
        void DrawNativeScriptInspector(NativeScriptComponent& component);
    } // namespace PropertiesPanelCustomDrawers
} // namespace Engine
