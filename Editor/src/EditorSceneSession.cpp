#include "EditorSceneSession.h"

#include "Core/Log.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Components.h"

namespace Engine
{
    namespace
    {
        size_t CountSceneEntities(const Ref<Scene>& scene)
        {
            if (!scene)
                return 0;

            size_t count = 0;
            for (auto entity : scene->GetRegistry().view<IDComponent>())
            {
                (void)entity;
                ++count;
            }
            return count;
        }
    } // namespace

    void EditorSceneSession::Initialize(SceneRenderer* sceneRenderer)
    {
        m_SceneRenderer = sceneRenderer;
        m_EditorSceneSnapshot = nullptr;
        m_State = SceneState::Edit;
    }

    void EditorSceneSession::CreateNewScene(Ref<Scene>& activeScene, uint32_t viewportWidth, uint32_t viewportHeight)
    {
        if (m_State == SceneState::Play)
            EndPlay(activeScene);

        activeScene = CreateRef<Scene>();
        if (m_SceneRenderer)
            activeScene->SetSceneRenderer(m_SceneRenderer);
        activeScene->OnViewportResize(viewportWidth, viewportHeight);

        ENGINE_INFO("[EditorEvent] NewScene ready, entities={0}", CountSceneEntities(activeScene));
    }

    bool EditorSceneSession::OpenSceneFromPath(Ref<Scene>& activeScene, const std::string& filepath,
                                               uint32_t viewportWidth, uint32_t viewportHeight,
                                               EditorRenderSettings* outRenderSettings)
    {
        if (filepath.empty())
            return false;

        if (m_State == SceneState::Play)
            EndPlay(activeScene);

        auto newScene = CreateRef<Scene>();
        if (m_SceneRenderer)
            newScene->SetSceneRenderer(m_SceneRenderer);

        EditorRenderSettings loadedRenderSettings;
        SceneSerializer serializer(newScene);
        if (!serializer.Deserialize(filepath, &loadedRenderSettings))
        {
            ENGINE_WARN("Failed to load scene from '{0}', keeping current scene", filepath);
            return false;
        }

        newScene->OnViewportResize(viewportWidth, viewportHeight);
        activeScene = newScene;

        if (outRenderSettings)
            *outRenderSettings = loadedRenderSettings;

        ENGINE_INFO("[EditorEvent] OpenScene loaded '{0}', entities={1}", filepath, CountSceneEntities(activeScene));
        return true;
    }

    bool EditorSceneSession::SaveSceneToPath(const Ref<Scene>& activeScene, const std::string& filepath,
                                             const EditorRenderSettings& renderSettings)
    {
        if (!activeScene || filepath.empty())
            return false;

        SceneSerializer serializer(activeScene);
        if (serializer.Serialize(filepath, renderSettings))
        {
            ENGINE_INFO("Scene saved to '{0}'", filepath);
            return true;
        }

        ENGINE_WARN("Failed to save scene to '{0}'", filepath);
        return false;
    }

    void EditorSceneSession::BeginPlay(Ref<Scene>& activeScene)
    {
        if (m_State == SceneState::Play || !activeScene || !m_SceneRenderer)
            return;

        ENGINE_INFO("[EditorEvent] ScenePlay requested, entities={0}", CountSceneEntities(activeScene));
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

        ENGINE_INFO("[EditorEvent] SceneStop requested");

        if (activeScene)
            activeScene->OnRuntimeStop();

        activeScene = m_EditorSceneSnapshot;
        m_EditorSceneSnapshot = nullptr;

        if (activeScene)
            m_SceneRenderer->GetShadowSystem().GetSettings() = activeScene->GetShadowSettings();

        m_State = SceneState::Edit;
        ENGINE_INFO("[EditorEvent] SceneStop completed, restored entities={0}", CountSceneEntities(activeScene));
    }

} // namespace Engine
