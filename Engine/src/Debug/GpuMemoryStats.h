#pragma once

#include "Core/Base.h"
#include "Debug/GpuMemCategory.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Texture.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine
{

    // 格式字节数/像素（引擎侧估算口径）
    uint64_t GpuTextureFormatBPP(TextureFormat format);
    uint64_t GpuFramebufferFormatBPP(FramebufferTextureFormat format);

    // 字节数人性化标签（用于面板资源明细，如 "12.4 MB"）
    std::string GpuMemFormatBytes(uint64_t bytes);

    struct GpuMemEntrySnapshot
    {
        uint64_t       Handle    = 0;
        GpuMemCategory Category  = GpuMemCategory::Other;
        uint64_t       Bytes     = 0;
        uint32_t       Triangles = 0; // 仅索引缓冲非零（驻留三角面）
        std::string    Label;
    };

    // GPU 资源显存记账单例：
    // - 门面工厂创建资源时登记，弱引用存活跟踪，资源销毁后条目在下次
    //   CollectLiveEntries 时自动清除（无需侵入各基类析构函数）
    // - 上行/下行累计字节计数器（带宽速率由面板做差分计算）
    // 数值为引擎估算口径（宽x高xbp，含 mip 时 x4/3），不含驱动对齐开销。
    class GpuMemoryStats
    {
    public:
        static GpuMemoryStats& Get()
        {
            static GpuMemoryStats instance;
            return instance;
        }

        GpuMemoryStats(const GpuMemoryStats&)            = delete;
        GpuMemoryStats& operator=(const GpuMemoryStats&) = delete;

        // 登记资源；triangles 仅索引缓冲填写（count/3），用于驻留三角面统计
        template <typename T>
        void TrackResource(
            const Ref<T>& res, GpuMemCategory category, uint64_t bytes, std::string label, uint32_t triangles = 0)
        {
            if (!res)
                return;

            std::scoped_lock lock(m_Mutex);
            Entry            entry;
            entry.Weak      = res;
            entry.Category  = category;
            entry.Bytes     = bytes;
            entry.Triangles = triangles;
            entry.Label     = std::move(label);
            m_Entries.emplace(m_NextHandle++, std::move(entry));
        }

        void AddUploaded(uint64_t bytes) { m_UploadedBytes.fetch_add(bytes, std::memory_order_relaxed); }

        void AddDownloaded(uint64_t bytes) { m_DownloadedBytes.fetch_add(bytes, std::memory_order_relaxed); }

        uint64_t GetUploadedBytes() const { return m_UploadedBytes.load(std::memory_order_relaxed); }

        uint64_t GetDownloadedBytes() const { return m_DownloadedBytes.load(std::memory_order_relaxed); }

        void     SetParticleCount(uint32_t count) { m_ParticleCount.store(count, std::memory_order_relaxed); }
        uint32_t GetParticleCount() const { return m_ParticleCount.load(std::memory_order_relaxed); }

        // 收集存活资源快照，并顺带清理已销毁资源的条目（编辑器面板每帧调用）
        std::vector<GpuMemEntrySnapshot> CollectLiveEntries();

        // 存活资源字节总数（顺带清理已销毁条目；基准测试每轮开始时调用）
        uint64_t TotalAllocatedBytes();

        // 进程工作集字节数（Windows 实现，其他平台返回 0）
        static uint64_t QueryProcessWorkingSetBytes();

    private:
        GpuMemoryStats() = default;

        struct Entry
        {
            std::weak_ptr<const void> Weak;
            GpuMemCategory            Category  = GpuMemCategory::Other;
            uint64_t                  Bytes     = 0;
            uint32_t                  Triangles = 0;
            std::string               Label;
        };

        std::mutex                          m_Mutex;
        std::unordered_map<uint64_t, Entry> m_Entries;
        uint64_t                            m_NextHandle = 1;
        std::atomic<uint64_t>               m_UploadedBytes{0};
        std::atomic<uint64_t>               m_DownloadedBytes{0};
        std::atomic<uint32_t>               m_ParticleCount{0};
    };

} // namespace Engine
