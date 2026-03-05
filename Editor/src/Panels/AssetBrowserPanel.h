#pragma once

#include "Core/Base.h"
#include "Scene/Scene.h"

#include <string>
#include <vector>
#include <filesystem>
#include <functional>

namespace Engine
{

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

    private:
        std::filesystem::path m_RootDirectory;
        std::filesystem::path m_CurrentDirectory;

        Ref<Scene> m_Scene;
        SceneOpenCallback m_OnSceneOpen;

        // 图片预览
        bool m_ShowImagePreview = false;
        std::string m_PreviewImagePath;
        uint32_t m_PreviewTextureID = 0;
        int m_PreviewImageWidth = 0;
        int m_PreviewImageHeight = 0;
    };

} // namespace Engine
