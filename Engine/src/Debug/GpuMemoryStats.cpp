#include "engpch.h"
#include "Debug/GpuMemoryStats.h"

#include <sstream>

namespace Engine
{

    uint64_t GpuTextureFormatBPP(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::RGBA8:    return 4;
        case TextureFormat::RGBA16F:  return 8;
        case TextureFormat::RG16F:    return 4;
        case TextureFormat::R32F:     return 4;
        case TextureFormat::R16F:     return 2;
        }
        return 0;
    }

    uint64_t GpuFramebufferFormatBPP(FramebufferTextureFormat format)
    {
        switch (format)
        {
        case FramebufferTextureFormat::RGBA8:            return 4;
        case FramebufferTextureFormat::RGBA16F:          return 8;
        case FramebufferTextureFormat::RED_INTEGER:      return 4;
        case FramebufferTextureFormat::R32F:             return 4;
        case FramebufferTextureFormat::R16F:             return 2;
        case FramebufferTextureFormat::DEPTH24STENCIL8:  return 4;
        case FramebufferTextureFormat::DEPTH_COMPONENT:  return 4;
        case FramebufferTextureFormat::None:             break;
        }
        return 0;
    }

    std::string GpuMemFormatBytes(uint64_t bytes)
    {
        std::ostringstream oss;
        if (bytes >= 1024ull * 1024 * 1024)
        {
            oss.precision(2);
            oss << static_cast<double>(bytes) / (1024.0 * 1024 * 1024) << " GB";
        }
        else if (bytes >= 1024ull * 1024)
        {
            oss.precision(1);
            oss << static_cast<double>(bytes) / (1024.0 * 1024) << " MB";
        }
        else if (bytes >= 1024ull)
        {
            oss.precision(1);
            oss << static_cast<double>(bytes) / 1024.0 << " KB";
        }
        else
        {
            oss << bytes << " B";
        }
        return oss.str();
    }

    std::vector<GpuMemEntrySnapshot> GpuMemoryStats::CollectLiveEntries()
    {
        std::vector<GpuMemEntrySnapshot> live;
        std::scoped_lock                 lock(m_Mutex);

        for (auto it = m_Entries.begin(); it != m_Entries.end();)
        {
            if (it->second.Weak.expired())
            {
                it = m_Entries.erase(it);
                continue;
            }

            GpuMemEntrySnapshot snapshot;
            snapshot.Handle    = it->first;
            snapshot.Category  = it->second.Category;
            snapshot.Bytes     = it->second.Bytes;
            snapshot.Triangles = it->second.Triangles;
            snapshot.Label     = it->second.Label;
            live.push_back(std::move(snapshot));
            ++it;
        }

        return live;
    }

} // namespace Engine
