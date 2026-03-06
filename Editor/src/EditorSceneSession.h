#pragma once

#include "Core/Base.h"
#include "Scene/Scene.h"

namespace Engine
{

    enum class SceneState { Edit = 0, Play = 1 };

    class SceneRenderer;

    class EditorSceneSession
    {
    public:
        void Initialize(SceneRenderer* sceneRenderer);

        SceneState GetState() const { return m_State; }
        bool IsPlaying() const { return m_State == SceneState::Play; }

        void BeginPlay(Ref<Scene>& activeScene);
        void EndPlay(Ref<Scene>& activeScene);

    private:
        SceneRenderer* m_SceneRenderer = nullptr;
        Ref<Scene> m_EditorSceneSnapshot;
        SceneState m_State = SceneState::Edit;
    };

} // namespace Engine
