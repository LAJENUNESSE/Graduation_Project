#include "Panels/AssetBrowserPanel.h"
#include "Core/Log.h"
#include "Renderer/Texture.h"
#include "Asset/PathUtils.h"
#include "Scene/SceneSerializer.h"

#include <imgui.h>
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif
namespace Engine
{

    // 支持的图片扩展名
    static bool IsImageFile(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".bmp" || ext == ".tga" || ext == ".hdr";
    }

    static bool IsSceneFile(const std::filesystem::path& path)
    {
        return path.extension() == ".scene";
    }

    static bool IsShaderFile(const std::filesystem::path& path)
    {
        return path.extension() == ".glsl";
    }

    static bool IsModelFile(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb";
    }

    static bool IsAudioFile(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".wav" || ext == ".mp3" || ext == ".ogg";
    }

    AssetBrowserPanel::AssetBrowserPanel()
    {
        m_RootDirectory = PathUtils::GetAssetRoot();
        m_CurrentDirectory = m_RootDirectory;
    }

    const char* AssetBrowserPanel::GetFileIcon(const std::filesystem::path& path)
    {
        if (std::filesystem::is_directory(path))
            return "[D]";
        if (IsImageFile(path))
            return "[I]";
        if (IsSceneFile(path))
            return "[S]";
        if (IsShaderFile(path))
            return "[G]";
        if (IsModelFile(path))
            return "[M]";
        if (IsAudioFile(path))
            return "[A]";
        return "[F]";
    }

    void AssetBrowserPanel::OnImGuiRender()
    {
        ImGui::Begin("\xe8\xb5\x84\xe4\xba\xa7\xe6\xb5\x8f\xe8\xa7\x88\xe5\x99\xa8"); // 资产浏览器

        // 确保根目录存在
        if (!std::filesystem::exists(m_RootDirectory))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                "\xe6\x9c\xaa\xe6\x89\xbe\xe5\x88\xb0 assets/ \xe7\x9b\xae\xe5\xbd\x95"); // 未找到 assets/ 目录
            ImGui::End();
            return;
        }

        // 面包屑导航
        DrawBreadcrumbs();

        ImGui::Separator();

        // 双列布局
        float panelWidth = ImGui::GetContentRegionAvail().x;
        float treeWidth = (std::max)(panelWidth * 0.25f, 150.0f);

        // 左侧：文件夹树
        ImGui::BeginChild("FolderTree", ImVec2(treeWidth, 0), true);
        DrawDirectoryTree(m_RootDirectory);
        ImGui::EndChild();

        ImGui::SameLine();

        // 右侧：文件网格
        ImGui::BeginChild("FileGrid", ImVec2(0, 0), true);
        DrawFileGrid();
        ImGui::EndChild();

        // 图片预览窗口
        DrawImagePreview();

        ImGui::End();
    }

    void AssetBrowserPanel::DrawBreadcrumbs()
    {
        // 后退按钮
        bool canGoBack = m_CurrentDirectory != m_RootDirectory;
        if (!canGoBack)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
        }
        if (ImGui::Button("<-") && canGoBack)
            m_CurrentDirectory = m_CurrentDirectory.parent_path();
        if (!canGoBack)
            ImGui::PopStyleColor(3);

        ImGui::SameLine();

        // 从 root 到 current 路径的每一级
        std::filesystem::path relative = std::filesystem::relative(m_CurrentDirectory, m_RootDirectory);

        if (ImGui::Button("assets"))
            m_CurrentDirectory = m_RootDirectory;

        if (relative != "." && !relative.empty())
        {
            std::filesystem::path accumulated = m_RootDirectory;
            for (const auto& part : relative)
            {
                accumulated /= part;
                ImGui::SameLine();
                ImGui::TextUnformatted("/");
                ImGui::SameLine();
                if (ImGui::Button(part.string().c_str()))
                    m_CurrentDirectory = accumulated;
            }
        }
    }

    void AssetBrowserPanel::DrawDirectoryTree(const std::filesystem::path& directory)
    {
        std::string dirName = directory.filename().string();
        if (dirName.empty())
            dirName = directory.string();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_CurrentDirectory == directory)
            flags |= ImGuiTreeNodeFlags_Selected;

        // 检查是否有子目录
        bool hasSubDirs = false;
        if (std::filesystem::exists(directory))
        {
            for (const auto& entry : std::filesystem::directory_iterator(directory))
            {
                if (entry.is_directory())
                {
                    hasSubDirs = true;
                    break;
                }
            }
        }

        if (!hasSubDirs)
            flags |= ImGuiTreeNodeFlags_Leaf;

        bool opened = ImGui::TreeNodeEx(dirName.c_str(), flags);

        if (ImGui::IsItemClicked())
            m_CurrentDirectory = directory;

        if (opened)
        {
            if (std::filesystem::exists(directory))
            {
                // 收集子目录并排序
                std::vector<std::filesystem::path> subDirs;
                for (const auto& entry : std::filesystem::directory_iterator(directory))
                {
                    if (entry.is_directory())
                        subDirs.push_back(entry.path());
                }
                std::sort(subDirs.begin(), subDirs.end());

                for (const auto& subDir : subDirs)
                    DrawDirectoryTree(subDir);
            }
            ImGui::TreePop();
        }
    }

    void AssetBrowserPanel::DrawFileGrid()
    {
        if (!std::filesystem::exists(m_CurrentDirectory))
            return;

        // 右键菜单
        if (ImGui::BeginPopupContextWindow("AssetBrowserContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("\xe5\x88\xb7\xe6\x96\xb0")) // 刷新
            {
                // 强制刷新（不需要做什么，下次迭代会自动重新读取）
            }
#ifdef _WIN32
            if (ImGui::MenuItem("\xe5\x9c\xa8\xe6\x96\x87\xe4\xbb\xb6\xe7\xae\xa1\xe7\x90\x86\xe5\x99\xa8\xe4\xb8\xad\xe6\x89\x93\xe5\xbc\x80")) // 在文件管理器中打开
            {
                ShellExecuteA(nullptr, "explore", m_CurrentDirectory.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
#endif
            ImGui::EndPopup();
        }

        // 收集并排序条目（文件夹优先，然后按名称）
        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory))
            entries.push_back(entry);

        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            if (a.is_directory() != b.is_directory())
                return a.is_directory();
            return a.path().filename() < b.path().filename();
        });

        // 网格参数
        float cellSize = 80.0f;
        float padding = 8.0f;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (std::max)(1, static_cast<int>(panelWidth / (cellSize + padding)));

        ImGui::Columns(columnCount, nullptr, false);

        for (const auto& entry : entries)
        {
            const auto& path = entry.path();
            std::string filename = path.filename().string();
            const char* icon = GetFileIcon(path);

            ImGui::PushID(filename.c_str());

            // 按钮表示文件/文件夹
            std::string label = std::string(icon) + "\n" + filename;

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));

            if (ImGui::Button(label.c_str(), ImVec2(cellSize, cellSize)))
            {
                // 单击：选择
            }

            ImGui::PopStyleColor(2);

            // 拖拽源：将文件相对路径作为 payload
            if (!entry.is_directory() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                // 计算相对于项目根目录的路径
                std::string relStr = PathUtils::ToProjectRelativeOrAbsolute(path);

                // 根据文件类型设置不同的 payload 类型
                const char* payloadType = "ASSET_PATH";
                if (IsImageFile(path))
                    payloadType = "ASSET_TEXTURE";
                else if (IsModelFile(path))
                    payloadType = "ASSET_MODEL";
                else if (IsSceneFile(path))
                    payloadType = "ASSET_SCENE";
                else if (IsAudioFile(path))
                    payloadType = "ASSET_AUDIO";

                ImGui::SetDragDropPayload(payloadType, relStr.c_str(), relStr.size() + 1);
                ImGui::Text("%s %s", icon, filename.c_str());
                ImGui::EndDragDropSource();
            }

            // 双击处理
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (entry.is_directory())
                    m_CurrentDirectory = path;
                else
                    OnFileDoubleClicked(path);
            }

            // 右键菜单（单个文件）
            if (ImGui::BeginPopupContextItem())
            {
#ifdef _WIN32
                if (ImGui::MenuItem("\xe5\x9c\xa8\xe6\x96\x87\xe4\xbb\xb6\xe7\xae\xa1\xe7\x90\x86\xe5\x99\xa8\xe4\xb8\xad\xe6\x98\xbe\xe7\xa4\xba")) // 在文件管理器中显示
                {
                    std::string cmd = "/select," + path.string();
                    ShellExecuteA(nullptr, "open", "explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
                }
#endif
                ImGui::EndPopup();
            }

            // 工具提示
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("%s", filename.c_str());
                if (!entry.is_directory())
                {
                    auto fileSize = entry.file_size();
                    if (fileSize < 1024)
                        ImGui::Text("%llu B", fileSize);
                    else if (fileSize < 1024 * 1024)
                        ImGui::Text("%.1f KB", fileSize / 1024.0f);
                    else
                        ImGui::Text("%.1f MB", fileSize / (1024.0f * 1024.0f));
                }
                ImGui::EndTooltip();
            }

            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);
    }

    void AssetBrowserPanel::OnFileDoubleClicked(const std::filesystem::path& path)
    {
        if (IsSceneFile(path))
        {
            // 通过回调加载场景
            if (m_OnSceneOpen)
                m_OnSceneOpen(PathUtils::ToProjectRelativeOrAbsolute(path));
            else
                ENGINE_INFO("双击场景文件: {0}", path.string());
        }
        else if (IsImageFile(path))
        {
            // 打开图片预览
            m_ShowImagePreview = true;
            ClearImagePreview();
            m_PreviewImagePath = PathUtils::ToProjectRelativeOrAbsolute(path);

            // 持有纹理对象，避免只保存 renderer ID 导致悬空资源
            m_PreviewTexture = Texture2D::Create(m_PreviewImagePath);
            if (m_PreviewTexture)
            {
                m_PreviewImageWidth = static_cast<int>(m_PreviewTexture->GetWidth());
                m_PreviewImageHeight = static_cast<int>(m_PreviewTexture->GetHeight());
            }
            else
            {
                ENGINE_WARN("Failed to load preview texture: {0}", path.string());
            }
        }
    }

    void AssetBrowserPanel::ClearImagePreview()
    {
        m_PreviewTexture.reset();
        m_PreviewImagePath.clear();
        m_PreviewImageWidth = 0;
        m_PreviewImageHeight = 0;
    }

    void AssetBrowserPanel::DrawImagePreview()
    {
        if (!m_ShowImagePreview)
            return;

        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("图片预览", &m_ShowImagePreview)) // 图片预览
        {
            ImGui::Text("%s", m_PreviewImagePath.c_str());
            ImGui::Text("尺寸: %d x %d", m_PreviewImageWidth, m_PreviewImageHeight); // 尺寸

            if (m_PreviewTexture)
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                float aspect = static_cast<float>(m_PreviewImageWidth) / static_cast<float>(m_PreviewImageHeight);
                float w = avail.x;
                float h = w / aspect;
                if (h > avail.y)
                {
                    h = avail.y;
                    w = h * aspect;
                }
                ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(m_PreviewTexture->GetRendererID())),
                             ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
            }
        }
        ImGui::End();

        if (!m_ShowImagePreview)
            ClearImagePreview();
    }

} // namespace Engine

