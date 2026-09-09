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

    // 运行时驱动模式：本地步进 vs 联机（服务器权威，本地只应用快照）
    enum class RuntimeMode
    {
        Local         = 0,
        NetworkClient = 1
    };

    class SceneRenderer;

    class EditorSceneSession
    {
    public:
        explicit EditorSceneSession(SceneRenderer& sceneRenderer);
        void SetEditorScene(const Ref<Scene>& editorScene);

        SceneState        GetState() const { return m_State; }
        bool              IsPlaying() const { return m_State == SceneState::Play; }
        const Ref<Scene>& GetEditorScene() const { return m_EditorScene; }
        const Ref<Scene>& GetRuntimeScene() const { return m_RuntimeScene; }
        Ref<Scene>        GetSceneForSaving(const Ref<Scene>& activeScene) const;

        // 脏标记
        bool IsDirty() const { return m_Dirty; }
        void MarkDirty();
        void ClearDirty();

        // 场景路径追踪
        const std::string& GetCurrentScenePath() const { return m_CurrentScenePath; }
        void               SetCurrentScenePath(const std::string& path) { m_CurrentScenePath = path; }
        bool               HasScenePath() const { return !m_CurrentScenePath.empty(); }

        void CreateNewScene(Ref<Scene>& activeScene, uint32_t viewportWidth, uint32_t viewportHeight);
        bool OpenSceneFromPath(Ref<Scene>&           activeScene,
                               const std::string&    filepath,
                               uint32_t              viewportWidth,
                               uint32_t              viewportHeight,
                               EditorRenderSettings* outRenderSettings = nullptr);
        // 联机模式：从服务器下发的场景 YAML 字节加载
        bool OpenSceneFromMemory(Ref<Scene>&        activeScene,
                                 const std::string& yamlContent,
                                 uint32_t           viewportWidth,
                                 uint32_t           viewportHeight);
        bool SaveSceneToPath(const Ref<Scene>&           sceneToSave,
                             const std::string&          filepath,
                             const EditorRenderSettings& renderSettings);

        void BeginPlay(Ref<Scene>& activeScene);
        void EndPlay(Ref<Scene>& activeScene);

        RuntimeMode GetRuntimeMode() const { return m_RuntimeMode; }
        void        SetRuntimeMode(RuntimeMode mode) { m_RuntimeMode = mode; }

    private:
        // 创建场景 + 注入渲染器 + 视口尺寸 + 更新状态
        Ref<Scene> PrepareScene(Ref<Scene>& activeScene, uint32_t viewportWidth, uint32_t viewportHeight);

        SceneRenderer& m_SceneRenderer;
        Ref<Scene>     m_EditorScene;
        Ref<Scene>     m_RuntimeScene;
        SceneState     m_State       = SceneState::Edit;
        RuntimeMode    m_RuntimeMode = RuntimeMode::Local;
        bool           m_Dirty       = false;
        std::string    m_CurrentScenePath;
    };

} // namespace Engine
