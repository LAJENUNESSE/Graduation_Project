#pragma once

#include "Asset/AssetHandle.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Engine
{

    class FileWatcher
    {
    public:
        void Watch(const std::string& path, AssetHandle handle);
        void Unwatch(AssetHandle handle);
        std::vector<AssetHandle> CheckChanges(float deltaTime);

    private:
        struct Entry
        {
            std::string Path;
            std::filesystem::file_time_type LastWrite;
            AssetHandle Handle;
        };

        std::vector<Entry> m_Entries;
        float m_PollInterval = 2.0f;
        float m_Timer = 0.0f;
    };

} // namespace Engine
