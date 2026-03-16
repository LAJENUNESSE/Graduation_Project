#pragma once

#include "Core/Base.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

namespace Engine
{

    enum class SceneState
    {
        Edit = 0,
        Play = 1
    };

    class SceneRenderer;

    class EditorSceneSession
    {
    public:
        explicit EditorSceneSession(SceneRenderer& sceneRenderer);
        void SetEditorScene(const Ref<Scene>& editorScene);

        SceneState GetState() const { return m_State; }
        bool IsPlaying() const { return m_State == SceneState::Play; }
        const Ref<Scene>& GetEditorScene() const { return m_EditorScene; }
        const Ref<Scene>& GetRuntimeScene() const { return m_RuntimeScene; }
        Ref<Scene> GetSceneForSaving(const Ref<Scene>& activeScene) const;

        // 脏标记
        bool IsDirty() const { return m_Dirty; }
        void MarkDirty();
        void ClearDirty();

        // 场景路径追踪
        const std::string& GetCurrentScenePath() const { return m_CurrentScenePath; }
        void SetCurrentScenePath(const std::string& path) { m_CurrentScenePath = path; }
        bool HasScenePath() const { return !m_CurrentScenePath.empty(); }

        void CreateNewScene(Ref<Scene>& activeScene, uint32_t viewportWidth, uint32_t viewportHeight);
        bool OpenSceneFromPath(Ref<Scene>& activeScene, const std::string& filepath, uint32_t viewportWidth,
                               uint32_t viewportHeight, EditorRenderSettings* outRenderSettings = nullptr);
        bool SaveSceneToPath(const Ref<Scene>& sceneToSave, const std::string& filepath,
                             const EditorRenderSettings& renderSettings);

        void BeginPlay(Ref<Scene>& activeScene);
        void EndPlay(Ref<Scene>& activeScene);

    private:
        // 创建场景 + 注入渲染器 + 视口尺寸 + 更新状态
        Ref<Scene> PrepareScene(Ref<Scene>& activeScene, uint32_t viewportWidth, uint32_t viewportHeight);

        SceneRenderer& m_SceneRenderer;
        Ref<Scene> m_EditorScene;
        Ref<Scene> m_RuntimeScene;
        SceneState m_State = SceneState::Edit;
        bool m_Dirty = false;
        std::string m_CurrentScenePath;
    };

} // namespace Engine
