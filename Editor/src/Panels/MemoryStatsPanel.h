#pragma once

#include "Debug/GpuMemoryStats.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace Engine
{

    // 显存与内存监控面板：
    // - 占用总览：引擎分配显存的单一堆叠块（分类互斥着色）+ 图例 + CPU 工作集
    // - GPU 状态：NVML 温度/功耗/利用率（仅 N 卡，不可用时整节隐藏）
    // - 传输带宽：200ms 采样差分速率 + 会话峰值 + 迷你历史曲线
    // - 绘制统计：复用 PerformanceMonitor 每帧统计；驻留三角面来自记账簿
    // - 资源明细：存活资源 Top5
    class MemoryStatsPanel
    {
    public:
        void OnImGuiRender(bool* open);

    private:
        void SampleBandwidth();
        void DrawOverview(const uint64_t* catBytes, const uint32_t* catCounts);
        void DrawGpuStatus();
        void DrawBandwidth();
        void DrawRenderStats(uint64_t residentTriangles);
        void DrawTopResources(const std::vector<GpuMemEntrySnapshot>& entries);

    private:
        // 带宽采样（面板侧差分，200ms 周期，150 点 ≈ 30s 窗口）
        std::vector<float> m_UpHistoryMBps;
        std::vector<float> m_DownHistoryMBps;
        uint64_t           m_LastUploadedBytes   = 0;
        uint64_t           m_LastDownloadedBytes = 0;
        std::chrono::steady_clock::time_point m_LastSampleTime{};
        float              m_PeakUpMBps   = 0.0f;
        float              m_PeakDownMBps = 0.0f;

        bool m_NvmlTried = false;
    };

} // namespace Engine
