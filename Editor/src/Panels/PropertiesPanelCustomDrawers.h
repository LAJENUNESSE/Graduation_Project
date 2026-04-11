#pragma once

#include "Scene/Components.h"
#include "Script/NativeScriptComponent.h"

namespace Engine
{
    struct AudioRuntimeState;
    struct VideoRuntimeState;

    namespace PropertiesPanelCustomDrawers
    {
        bool DrawMeshRendererInspector(MeshRendererComponent& component);
        bool DrawTerrainInspector(TerrainComponent& component);
        bool DrawParticleEmitterInspector(ParticleEmitterComponent& component);
        bool DrawFluidEmitterInspector(FluidEmitterComponent& component);
        bool DrawAudioSourceInspector(AudioSourceComponent& component, const AudioRuntimeState* runtimeState = nullptr);
        bool DrawAudioListenerInspector(AudioListenerComponent& component);
        bool DrawVideoPlayerInspector(VideoPlayerComponent& component, const VideoRuntimeState* runtimeState = nullptr);
        bool DrawNativeScriptInspector(NativeScriptComponent& component);
    } // namespace PropertiesPanelCustomDrawers
} // namespace Engine
