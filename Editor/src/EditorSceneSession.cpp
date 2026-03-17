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

    EditorSceneSession::EditorSceneSession(SceneRenderer& sceneRenderer) : m_SceneRenderer(sceneRenderer) {}

    void EditorSceneSession::MarkDirty()
    {
        m_Dirty = true;
    }

    void EditorSceneSession::ClearDirty()
    {
        m_Dirty = false;
    }

    void EditorSceneSession::SetEditorScene(const Ref<Scene>& editorScene)
    {
        m_EditorScene = editorScene;
        if (m_State == SceneState::Edit)
            m_RuntimeScene = nullptr;
    }

    Ref<Scene> EditorSceneSession::GetSceneForSaving(const Ref<Scene>& activeScene) const
    {
        if (m_EditorScene)
            return m_EditorScene;
        return activeScene;
    }

    Ref<Scene>
    EditorSceneSession::PrepareScene(Ref<Scene>& activeScene, uint32_t viewportWidth, uint32_t viewportHeight)
    {
        if (m_State == SceneState::Play)
            EndPlay(activeScene);

        auto newScene = CreateRef<Scene>();
        newScene->SetSceneRenderer(&m_SceneRenderer);
        newScene->OnViewportResize(viewportWidth, viewportHeight);

        m_EditorScene  = newScene;
        m_RuntimeScene = nullptr;
        activeScene    = m_EditorScene;

        return newScene;
    }

    void EditorSceneSession::CreateNewScene(Ref<Scene>& activeScene, uint32_t viewportWidth, uint32_t viewportHeight)
    {
        PrepareScene(activeScene, viewportWidth, viewportHeight);
        m_CurrentScenePath.clear();
        ClearDirty();
        ENGINE_INFO("[EditorEvent] NewScene ready, entities={0}", CountSceneEntities(activeScene));
    }

    bool EditorSceneSession::OpenSceneFromPath(Ref<Scene>&           activeScene,
                                               const std::string&    filepath,
                                               uint32_t              viewportWidth,
                                               uint32_t              viewportHeight,
                                               EditorRenderSettings* outRenderSettings)
    {
        if (filepath.empty())
            return false;

        if (m_State == SceneState::Play)
            EndPlay(activeScene);

        auto newScene = CreateRef<Scene>();

        EditorRenderSettings loadedRenderSettings;
        SceneSerializer      serializer(newScene);
        if (!serializer.Deserialize(filepath, &loadedRenderSettings))
        {
            ENGINE_WARN("Failed to load scene from '{0}', keeping current scene", filepath);
            return false;
        }

        // SetSceneRenderer 必须在 Deserialize 之后调用，
        // 才能将已加载的 shadow/skybox 正确推送到 renderer
        newScene->SetSceneRenderer(&m_SceneRenderer);

        newScene->OnViewportResize(viewportWidth, viewportHeight);
        m_EditorScene  = newScene;
        m_RuntimeScene = nullptr;
        activeScene    = m_EditorScene;

        if (outRenderSettings)
            *outRenderSettings = loadedRenderSettings;

        ENGINE_INFO("[EditorEvent] OpenScene loaded '{0}', entities={1}", filepath, CountSceneEntities(activeScene));
        m_CurrentScenePath = filepath;
        ClearDirty();
        return true;
    }

    bool EditorSceneSession::SaveSceneToPath(const Ref<Scene>&           sceneToSave,
                                             const std::string&          filepath,
                                             const EditorRenderSettings& renderSettings)
    {
        if (!sceneToSave || filepath.empty())
            return false;

        SceneSerializer serializer(sceneToSave);
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
        if (m_State == SceneState::Play || !activeScene)
            return;

        m_EditorScene = activeScene;
        ENGINE_INFO("[EditorEvent] ScenePlay requested, entities={0}", CountSceneEntities(m_EditorScene));

        m_RuntimeScene = Scene::Copy(m_EditorScene);
        m_RuntimeScene->SetSceneRenderer(&m_SceneRenderer);

        activeScene = m_RuntimeScene;
        activeScene->OnRuntimeStart();

        m_State = SceneState::Play;
        ENGINE_INFO("[EditorEvent] ScenePlay started");
    }

    void EditorSceneSession::EndPlay(Ref<Scene>& activeScene)
    {
        if (m_State != SceneState::Play)
            return;

        ENGINE_INFO("[EditorEvent] SceneStop requested");

        if (m_RuntimeScene)
            m_RuntimeScene->OnRuntimeStop();

        activeScene    = m_EditorScene;
        m_RuntimeScene = nullptr;

        if (activeScene)
        {
            auto& shadowSystem         = m_SceneRenderer.GetShadowSystem();
            shadowSystem.GetSettings() = activeScene->GetShadowSettings();
            shadowSystem.ResizeShadowMap(activeScene->GetShadowSettings().MapResolution);
            // 恢复 skybox（与 Scene::SetSceneRenderer 逻辑一致）
            const auto& skyboxPaths = activeScene->GetSkyboxFacePaths();
            if (skyboxPaths.empty())
                m_SceneRenderer.GetSkyboxSystem().ClearSkybox();
            else
                m_SceneRenderer.GetSkyboxSystem().LoadSkybox(skyboxPaths);
        }

        m_State = SceneState::Edit;
        ENGINE_INFO("[EditorEvent] SceneStop completed, restored entities={0}", CountSceneEntities(activeScene));
    }

} // namespace Engine
