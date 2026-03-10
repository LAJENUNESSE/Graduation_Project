#pragma once

#include "Core/Base.h"
#include "Scene/Scene.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Engine
{

    class Texture2D;

    class AssetBrowserPanel
    {
    public:
        AssetBrowserPanel();

        void SetContext(const Ref<Scene>& scene) { m_Scene = scene; }

        // 设置双击场景文件的回调（由 EditorLayer 设置以加载场景）
        using SceneOpenCallback = std::function<void(const std::string&)>;
        void SetSceneOpenCallback(SceneOpenCallback callback) { m_OnSceneOpen = std::move(callback); }

        void OnImGuiRender();

    private:
        bool EnsureCurrentDirectoryValid();
        bool TryEnumerateDirectory(const std::filesystem::path& directory,
                                   std::vector<std::filesystem::directory_entry>& outEntries);
        bool TryBuildRelativePath(const std::filesystem::path& path,
                                  const std::filesystem::path& base,
                                  std::filesystem::path& outRelative);
        bool TryGetFileSizeText(const std::filesystem::directory_entry& entry, std::string& outText);
        void SetFilesystemError(const std::string& message);

        // 绘制文件夹树（左侧）
        void DrawDirectoryTree(const std::filesystem::path& directory);

        // 绘制文件网格（右侧）
        void DrawFileGrid();

        // 绘制面包屑导航
        void DrawBreadcrumbs();

        // 获取文件类型图标
        static const char* GetFileIcon(const std::filesystem::path& path);

        // 处理文件双击
        void OnFileDoubleClicked(const std::filesystem::path& path);

        // 绘制图片预览窗口
        void DrawImagePreview();
        void ClearImagePreview();

    private:
        std::filesystem::path m_RootDirectory;
        std::filesystem::path m_CurrentDirectory;

        Ref<Scene> m_Scene;
        SceneOpenCallback m_OnSceneOpen;
        std::string m_LastFilesystemError;

        // 图片预览
        bool m_ShowImagePreview = false;
        std::string m_PreviewImagePath;
        Ref<Texture2D> m_PreviewTexture;
        int m_PreviewImageWidth = 0;
        int m_PreviewImageHeight = 0;
    };

} // namespace Engine