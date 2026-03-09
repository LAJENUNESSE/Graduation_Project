#pragma once

#include "Scene/Components.h"
#include "Script/NativeScriptComponent.h"

namespace Engine
{
    namespace PropertiesPanelCustomDrawers
    {
        void DrawMeshRendererInspector(MeshRendererComponent& component);
        void DrawTerrainInspector(TerrainComponent& component);
        void DrawParticleEmitterInspector(ParticleEmitterComponent& component);
        void DrawAudioSourceInspector(AudioSourceComponent& component);
        void DrawAudioListenerInspector(AudioListenerComponent& component);
        void DrawVideoPlayerInspector(VideoPlayerComponent& component);
        void DrawNativeScriptInspector(NativeScriptComponent& component);
    } // namespace PropertiesPanelCustomDrawers
} // namespace Engine
