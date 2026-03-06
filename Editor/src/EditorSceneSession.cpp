#include "EditorSceneSession.h"

#include "Core/Log.h"
#include "Renderer/SceneRenderer.h"

namespace Engine
{

    void EditorSceneSession::Initialize(SceneRenderer* sceneRenderer)
    {
        m_SceneRenderer = sceneRenderer;
        m_EditorSceneSnapshot = nullptr;
        m_State = SceneState::Edit;
    }

    void EditorSceneSession::BeginPlay(Ref<Scene>& activeScene)
    {
        if (m_State == SceneState::Play || !activeScene || !m_SceneRenderer)
            return;

        m_State = SceneState::Play;

        // 深拷贝当前场景作为编辑器快照
        m_EditorSceneSnapshot = Scene::Copy(activeScene);
        m_EditorSceneSnapshot->SetSceneRenderer(m_SceneRenderer);

        activeScene->OnRuntimeStart();
        ENGINE_INFO("[EditorEvent] ScenePlay started");
    }

    void EditorSceneSession::EndPlay(Ref<Scene>& activeScene)
    {
        if (m_State != SceneState::Play || !m_SceneRenderer)
            return;

        if (activeScene)
            activeScene->OnRuntimeStop();

        activeScene = m_EditorSceneSnapshot;
        m_EditorSceneSnapshot = nullptr;

        if (activeScene)
            m_SceneRenderer->GetShadowSystem().GetSettings() = activeScene->GetShadowSettings();

        m_State = SceneState::Edit;
    }

} // namespace Engine
