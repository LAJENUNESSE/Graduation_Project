#include "engpch.h"
#include "Asset/FileWatcher.h"
#include "Core/Log.h"

namespace Engine
{

    void FileWatcher::Watch(const std::string& path, AssetHandle handle)
    {
        // Don't watch builtin resources
        if (path.find("builtin:") != std::string::npos)
            return;

        std::error_code ec;
        auto lastWrite = std::filesystem::last_write_time(path, ec);
        if (ec)
            return;

        // Avoid duplicates
        for (auto& entry : m_Entries)
        {
            if (entry.Handle == handle)
            {
                entry.Path = path;
                entry.LastWrite = lastWrite;
                return;
            }
        }

        m_Entries.push_back({path, lastWrite, handle});
    }

    void FileWatcher::Unwatch(AssetHandle handle)
    {
        m_Entries.erase(
            std::remove_if(m_Entries.begin(), m_Entries.end(),
                [&](const Entry& e) { return e.Handle == handle; }),
            m_Entries.end());
    }

    std::vector<AssetHandle> FileWatcher::CheckChanges(float deltaTime)
    {
        std::vector<AssetHandle> changed;

        m_Timer += deltaTime;
        if (m_Timer < m_PollInterval)
            return changed;
        m_Timer = 0.0f;

        for (auto& entry : m_Entries)
        {
            std::error_code ec;
            auto currentWrite = std::filesystem::last_write_time(entry.Path, ec);
            if (ec)
                continue;

            if (currentWrite != entry.LastWrite)
            {
                entry.LastWrite = currentWrite;
                changed.push_back(entry.Handle);
            }
        }

        return changed;
    }

} // namespace Engine
