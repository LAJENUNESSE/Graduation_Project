#include "Panels/AssetBrowserPanel.h"
#include "Asset/AssetType.h"
#include "Asset/PathUtils.h"
#include "Core/Log.h"
#include "EditorAssetDescriptor.h"
#include "Renderer/Texture.h"
#include "Scene/SceneSerializer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

namespace Engine
{
    namespace
    {
        std::string FormatFileSize(uintmax_t fileSize)
        {
            char buffer[64] = {};
            if (fileSize < 1024)
            {
                std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(fileSize));
            }
            else if (fileSize < 1024ull * 1024ull)
            {
                std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(fileSize) / 1024.0);
            }
            else if (fileSize < 1024ull * 1024ull * 1024ull)
            {
                std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(fileSize) / (1024.0 * 1024.0));
            }
            else
            {
                std::snprintf(buffer, sizeof(buffer), "%.1f GB",
                              static_cast<double>(fileSize) / (1024.0 * 1024.0 * 1024.0));
            }

            return buffer;
        }

        bool IsDirectoryPath(const std::filesystem::path& path)
        {
            std::error_code ec;
            return std::filesystem::is_directory(path, ec) && !ec;
        }

        std::string
        BuildFilesystemError(const char* action, const std::filesystem::path& path, const std::error_code& ec)
        {
            return std::string(action) + ": " + PathUtils::PathToUtf8String(path) + " (" + ec.message() + ")";
        }

#ifdef _WIN32
        bool ExecuteExplorerCommand(const wchar_t* file, const wchar_t* parameters)
        {
            HINSTANCE result = ShellExecuteW(nullptr, L"open", file, parameters, nullptr, SW_SHOWNORMAL);
            return reinterpret_cast<intptr_t>(result) > 32;
        }

        void OpenPathInExplorer(const std::filesystem::path& path)
        {
            const std::filesystem::path resolved = PathUtils::ResolvePath(path);
            ExecuteExplorerCommand(resolved.c_str(), nullptr);
        }

        void RevealPathInExplorer(const std::filesystem::path& path)
        {
            const std::filesystem::path resolved   = PathUtils::ResolvePath(path);
            std::wstring                parameters = L"/select,\"";
            parameters += resolved.wstring();
            parameters += L"\"";
            ExecuteExplorerCommand(L"explorer.exe", parameters.c_str());
        }
#endif
    } // namespace

    AssetBrowserPanel::AssetBrowserPanel()
    {
        m_RootDirectory    = PathUtils::GetAssetRoot();
        m_CurrentDirectory = m_RootDirectory;
    }

    void AssetBrowserPanel::SetFilesystemError(const std::string& message)
    {
        if (!message.empty())
            m_LastFilesystemError = message;
    }

    bool AssetBrowserPanel::EnsureCurrentDirectoryValid()
    {
        std::error_code ec;
        bool            currentExists = std::filesystem::exists(m_CurrentDirectory, ec);
        if (ec)
        {
            SetFilesystemError(BuildFilesystemError("检查当前目录失败", m_CurrentDirectory, ec));
        }
        else if (currentExists && std::filesystem::is_directory(m_CurrentDirectory, ec) && !ec)
        {
            return true;
        }
        else if (!ec && !currentExists)
        {
            SetFilesystemError("当前目录已失效，已回退到 assets 根目录: " + m_CurrentDirectory.string());
        }
        else if (ec)
        {
            SetFilesystemError(BuildFilesystemError("检查当前目录失败", m_CurrentDirectory, ec));
        }
        else
        {
            SetFilesystemError("当前目录不可用，已回退到 assets 根目录: " + m_CurrentDirectory.string());
        }

        ec.clear();
        if (std::filesystem::exists(m_RootDirectory, ec) && !ec && std::filesystem::is_directory(m_RootDirectory, ec) &&
            !ec)
        {
            m_CurrentDirectory = m_RootDirectory;
            return true;
        }

        if (ec)
            SetFilesystemError(BuildFilesystemError("检查 assets 根目录失败", m_RootDirectory, ec));
        return false;
    }

    bool AssetBrowserPanel::TryEnumerateDirectory(const std::filesystem::path&                   directory,
                                                  std::vector<std::filesystem::directory_entry>& outEntries)
    {
        outEntries.clear();

        std::error_code ec;
        bool            exists = std::filesystem::exists(directory, ec);
        if (ec)
        {
            SetFilesystemError(BuildFilesystemError("检查目录失败", directory, ec));
            return false;
        }
        if (!exists)
        {
            SetFilesystemError("目录不存在: " + directory.string());
            return false;
        }

        if (!std::filesystem::is_directory(directory, ec) || ec)
        {
            SetFilesystemError(ec ? BuildFilesystemError("检查目录类型失败", directory, ec)
                                  : "路径不是目录: " + directory.string());
            return false;
        }

        std::filesystem::directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied,
                                               ec);
        if (ec)
        {
            SetFilesystemError(BuildFilesystemError("打开目录失败", directory, ec));
            return false;
        }

        const std::filesystem::directory_iterator end;
        while (it != end)
        {
            outEntries.push_back(*it);
            it.increment(ec);
            if (ec)
            {
                SetFilesystemError(BuildFilesystemError("遍历目录失败", directory, ec));
                break;
            }
        }

        return true;
    }

    bool AssetBrowserPanel::TryBuildRelativePath(const std::filesystem::path& path,
                                                 const std::filesystem::path& base,
                                                 std::filesystem::path&       outRelative)
    {
        std::error_code ec;
        outRelative = std::filesystem::relative(path, base, ec);
        if (ec)
        {
            SetFilesystemError(BuildFilesystemError("计算相对路径失败", path, ec));
            return false;
        }

        return true;
    }

    bool AssetBrowserPanel::TryGetFileSizeText(const std::filesystem::directory_entry& entry, std::string& outText)
    {
        outText.clear();

        std::error_code ec;
        uintmax_t       fileSize = entry.file_size(ec);
        if (ec)
        {
            SetFilesystemError(BuildFilesystemError("读取文件大小失败", entry.path(), ec));
            return false;
        }

        outText = FormatFileSize(fileSize);
        return true;
    }

    const char* AssetBrowserPanel::GetFileIcon(const std::filesystem::path& path)
    {
        if (IsDirectoryPath(path))
            return "[D]";
        AssetType type = AssetTypeFromPath(path);
        if (type != AssetType::None)
            return GetEditorAssetDescriptor(type).Icon;
        return "[F]";
    }

    void AssetBrowserPanel::InvalidateDirectoryCache()
    {
        m_DirectoryCache.clear();
    }

    bool AssetBrowserPanel::TryEnumerateDirectoryCached(const std::filesystem::path&                   directory,
                                                        std::vector<std::filesystem::directory_entry>& outEntries)
    {
        // 每秒过期一次，刷新全部缓存
        auto  now     = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - m_CacheTimestamp).count();
        if (elapsed > 1.0f)
        {
            m_DirectoryCache.clear();
            m_CacheTimestamp = now;
        }

        std::string key = directory.string();
        auto        it  = m_DirectoryCache.find(key);
        if (it != m_DirectoryCache.end())
        {
            outEntries = it->second;
            return true;
        }

        if (!TryEnumerateDirectory(directory, outEntries))
            return false;

        m_DirectoryCache[key] = outEntries;
        return true;
    }

    void AssetBrowserPanel::OnImGuiRender()
    {
        m_LastFilesystemError.clear();
        ImGui::Begin("资产浏览器");

        std::error_code ec;
        bool            rootExists = std::filesystem::exists(m_RootDirectory, ec);
        if (ec)
        {
            SetFilesystemError(BuildFilesystemError("检查 assets 根目录失败", m_RootDirectory, ec));
            rootExists = false;
        }

        bool rootIsDirectory = false;
        if (rootExists)
        {
            rootIsDirectory = std::filesystem::is_directory(m_RootDirectory, ec);
            if (ec)
            {
                SetFilesystemError(BuildFilesystemError("检查 assets 根目录类型失败", m_RootDirectory, ec));
                rootIsDirectory = false;
            }
        }

        if (!rootExists || !rootIsDirectory)
        {
            if (m_LastFilesystemError.empty())
                SetFilesystemError("未找到 assets 目录: " + m_RootDirectory.string());
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", m_LastFilesystemError.c_str());
            ImGui::End();
            return;
        }

        EnsureCurrentDirectoryValid();

        if (!m_LastFilesystemError.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", m_LastFilesystemError.c_str());
            ImGui::Separator();
        }

        DrawBreadcrumbs();
        ImGui::Separator();

        float panelWidth = ImGui::GetContentRegionAvail().x;
        float treeWidth  = (std::max)(panelWidth * 0.25f, 150.0f);

        ImGui::BeginChild("FolderTree", ImVec2(treeWidth, 0), true);
        DrawDirectoryTree(m_RootDirectory);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("FileGrid", ImVec2(0, 0), true);
        DrawFileGrid();
        ImGui::EndChild();

        DrawImagePreview();

        ImGui::End();
    }

    void AssetBrowserPanel::DrawBreadcrumbs()
    {
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

        std::filesystem::path relative;
        if (!TryBuildRelativePath(m_CurrentDirectory, m_RootDirectory, relative))
        {
            if (ImGui::Button("assets"))
                m_CurrentDirectory = m_RootDirectory;
            return;
        }

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

        std::vector<std::filesystem::directory_entry> entries;
        bool                                          hasSubDirs = false;
        if (TryEnumerateDirectoryCached(directory, entries))
        {
            for (const auto& entry : entries)
            {
                std::error_code ec;
                if (entry.is_directory(ec) && !ec)
                {
                    hasSubDirs = true;
                    break;
                }
                if (ec)
                {
                    SetFilesystemError(BuildFilesystemError("检查子目录失败", entry.path(), ec));
                }
            }
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_CurrentDirectory == directory)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (!hasSubDirs)
            flags |= ImGuiTreeNodeFlags_Leaf;

        bool opened = ImGui::TreeNodeEx(dirName.c_str(), flags);

        if (ImGui::IsItemClicked())
            m_CurrentDirectory = directory;

        if (opened)
        {
            std::vector<std::filesystem::path> subDirs;
            subDirs.reserve(entries.size());
            for (const auto& entry : entries)
            {
                std::error_code ec;
                bool            isDirectory = entry.is_directory(ec);
                if (ec)
                {
                    SetFilesystemError(BuildFilesystemError("检查子目录失败", entry.path(), ec));
                    continue;
                }
                if (isDirectory)
                    subDirs.push_back(entry.path());
            }

            std::sort(subDirs.begin(), subDirs.end());
            for (const auto& subDir : subDirs)
                DrawDirectoryTree(subDir);

            ImGui::TreePop();
        }
    }

    void AssetBrowserPanel::DrawFileGrid()
    {
        if (!EnsureCurrentDirectoryValid())
            return;

        if (ImGui::BeginPopupContextWindow("AssetBrowserContextMenu",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("刷新"))
                InvalidateDirectoryCache();
#ifdef _WIN32
            if (ImGui::MenuItem("在文件管理器中打开"))
            {
                OpenPathInExplorer(m_CurrentDirectory);
            }
#endif
            ImGui::EndPopup();
        }

        std::vector<std::filesystem::directory_entry> entries;
        if (!TryEnumerateDirectoryCached(m_CurrentDirectory, entries))
            return;

        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b)
                  {
                      std::error_code aEc;
                      std::error_code bEc;
                      bool            aIsDirectory = a.is_directory(aEc) && !aEc;
                      bool            bIsDirectory = b.is_directory(bEc) && !bEc;
                      if (aIsDirectory != bIsDirectory)
                          return aIsDirectory;
                      return a.path().filename() < b.path().filename();
                  });

        float cellSize    = 80.0f;
        float padding     = 8.0f;
        float panelWidth  = ImGui::GetContentRegionAvail().x;
        int   columnCount = (std::max)(1, static_cast<int>(panelWidth / (cellSize + padding)));

        ImGui::Columns(columnCount, nullptr, false);

        for (const auto& entry : entries)
        {
            const auto& path     = entry.path();
            std::string filename = path.filename().string();
            const char* icon     = GetFileIcon(path);

            ImGui::PushID(filename.c_str());

            std::string label = std::string(icon) + "\n" + filename;

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGui::Button(label.c_str(), ImVec2(cellSize, cellSize));
            ImGui::PopStyleColor(2);

            std::error_code dirEc;
            bool            isDirectory = entry.is_directory(dirEc);
            if (dirEc)
            {
                SetFilesystemError(BuildFilesystemError("检查条目类型失败", path, dirEc));
                isDirectory = false;
            }

            if (!isDirectory && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                std::string relStr = PathUtils::ToProjectRelativeOrAbsolute(path);

                AssetType   assetType   = AssetTypeFromPath(path);
                const char* payloadType = GetEditorAssetDescriptor(assetType).PayloadType;

                ImGui::SetDragDropPayload(payloadType, relStr.c_str(), relStr.size() + 1);
                ImGui::Text("%s %s", icon, filename.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (isDirectory)
                    m_CurrentDirectory = path;
                else
                    OnFileDoubleClicked(path);
            }

            if (ImGui::BeginPopupContextItem())
            {
#ifdef _WIN32
                if (ImGui::MenuItem("在文件管理器中显示"))
                {
                    RevealPathInExplorer(path);
                }
#endif
                ImGui::EndPopup();
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("%s", filename.c_str());
                if (!isDirectory)
                {
                    std::string fileSizeText;
                    if (TryGetFileSizeText(entry, fileSizeText) && !fileSizeText.empty())
                        ImGui::Text("%s", fileSizeText.c_str());
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
        AssetType type = AssetTypeFromPath(path);
        switch (type)
        {
        case AssetType::Scene:
            if (m_OnSceneOpen)
                m_OnSceneOpen(PathUtils::ToProjectRelativeOrAbsolute(path));
            else
                ENGINE_INFO("双击场景文件: {0}", path.string());
            break;

        case AssetType::Script:
            if (m_OnScriptOpen)
                m_OnScriptOpen(PathUtils::ToProjectRelativeOrAbsolute(path));
            else
                ENGINE_INFO("双击脚本文件: {0}", path.string());
            break;

        case AssetType::Texture2D:
            m_ShowImagePreview = true;
            ClearImagePreview();
            m_PreviewImagePath = PathUtils::ToProjectRelativeOrAbsolute(path);

            m_PreviewTexture = Texture2D::Create(m_PreviewImagePath);
            if (m_PreviewTexture)
            {
                m_PreviewImageWidth  = static_cast<int>(m_PreviewTexture->GetWidth());
                m_PreviewImageHeight = static_cast<int>(m_PreviewTexture->GetHeight());
            }
            else
            {
                ENGINE_WARN("Failed to load preview texture: {0}", path.string());
            }
            break;

        default:
            break;
        }
    }

    void AssetBrowserPanel::ClearImagePreview()
    {
        m_PreviewTexture.reset();
        m_PreviewImagePath.clear();
        m_PreviewImageWidth  = 0;
        m_PreviewImageHeight = 0;
    }

    void AssetBrowserPanel::DrawImagePreview()
    {
        if (!m_ShowImagePreview)
            return;

        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("图片预览", &m_ShowImagePreview))
        {
            ImGui::Text("%s", m_PreviewImagePath.c_str());
            ImGui::Text("尺寸: %d x %d", m_PreviewImageWidth, m_PreviewImageHeight);

            if (m_PreviewTexture)
            {
                ImVec2 avail  = ImGui::GetContentRegionAvail();
                float  aspect = static_cast<float>(m_PreviewImageWidth) / static_cast<float>(m_PreviewImageHeight);
                float  w      = avail.x;
                float  h      = w / aspect;
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