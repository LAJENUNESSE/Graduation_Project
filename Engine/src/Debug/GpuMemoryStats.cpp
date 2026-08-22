#include "engpch.h"
#include "Debug/GpuMemoryStats.h"

#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Engine
{

    uint64_t GpuTextureFormatBPP(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::RGBA8:
            return 4;
        case TextureFormat::RGBA16F:
            return 8;
        case TextureFormat::RG16F:
            return 4;
        case TextureFormat::R32F:
            return 4;
        case TextureFormat::R16F:
            return 2;
        }
        return 0;
    }

    uint64_t GpuFramebufferFormatBPP(FramebufferTextureFormat format)
    {
        switch (format)
        {
        case FramebufferTextureFormat::RGBA8:
            return 4;
        case FramebufferTextureFormat::RGBA16F:
            return 8;
        case FramebufferTextureFormat::RED_INTEGER:
            return 4;
        case FramebufferTextureFormat::R32F:
            return 4;
        case FramebufferTextureFormat::R16F:
            return 2;
        case FramebufferTextureFormat::DEPTH24STENCIL8:
            return 4;
        case FramebufferTextureFormat::DEPTH_COMPONENT:
            return 4;
        case FramebufferTextureFormat::None:
            break;
        }
        return 0;
    }

    std::string GpuMemFormatBytes(uint64_t bytes)
    {
        std::ostringstream oss;
        oss << std::fixed;
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

    uint64_t GpuMemoryStats::TotalAllocatedBytes()
    {
        std::scoped_lock lock(m_Mutex);
        uint64_t         total = 0;
        for (auto it = m_Entries.begin(); it != m_Entries.end();)
        {
            if (it->second.Weak.expired())
            {
                it = m_Entries.erase(it);
                continue;
            }
            total += it->second.Bytes;
            ++it;
        }
        return total;
    }

    uint64_t GpuMemoryStats::QueryProcessWorkingSetBytes()
    {
#ifdef _WIN32
        // 与 Win32 PROCESS_MEMORY_COUNTERS 布局一致，避免引入 psapi.h 链接依赖
        struct ProcessMemoryCountersLocal
        {
            DWORD  cb;
            DWORD  PageFaultCount;
            SIZE_T PeakWorkingSetSize;
            SIZE_T WorkingSetSize;
            SIZE_T QuotaPeakPagedPoolUsage;
            SIZE_T QuotaPagedPoolUsage;
            SIZE_T QuotaPeakNonPagedPoolUsage;
            SIZE_T QuotaNonPagedPoolUsage;
            SIZE_T PagefileUsage;
            SIZE_T PeakPagefileUsage;
        };
        using GetProcessMemoryInfoFn = BOOL (*)(HANDLE, ProcessMemoryCountersLocal*, DWORD);

        static GetProcessMemoryInfoFn fn    = nullptr;
        static bool                   tried = false;
        if (!tried)
        {
            tried = true;
            fn    = reinterpret_cast<GetProcessMemoryInfoFn>(reinterpret_cast<void*>(
                ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo")));
            if (!fn)
            {
                HMODULE psapi = ::LoadLibraryA("psapi.dll");
                if (psapi)
                    fn = reinterpret_cast<GetProcessMemoryInfoFn>(::GetProcAddress(psapi, "GetProcessMemoryInfo"));
            }
        }
        if (!fn)
            return 0;

        ProcessMemoryCountersLocal pmc{};
        pmc.cb = sizeof(pmc);
        if (fn(::GetCurrentProcess(), &pmc, sizeof(pmc)))
            return static_cast<uint64_t>(pmc.WorkingSetSize);
        return 0;
#else
        return 0;
#endif
    }

} // namespace Engine
