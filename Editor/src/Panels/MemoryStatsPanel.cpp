#include "Panels/MemoryStatsPanel.h"

#include "Debug/GpuMemoryStats.h"
#include "Debug/NvmlSampler.h"
#include "Debug/PerformanceMonitor.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Engine
{

    namespace
    {

        constexpr size_t kBandwidthHistoryMax = 150;
        constexpr float  kBandwidthSampleMs   = 200.0f;

        struct CatStyle
        {
            const char* Name;
            ImU32       Color;
        };

        // 与 GpuMemCategory 枚举顺序一一对应
        const CatStyle& CatStyleOf(GpuMemCategory cat)
        {
            static const CatStyle kStyles[] = {
                {"纹理", IM_COL32(0, 115, 217, 255)},        {"网格缓冲", IM_COL32(47, 168, 79, 255)},
                {"UBO/SSBO", IM_COL32(230, 179, 0, 255)},    {"粒子", IM_COL32(242, 113, 28, 255)},
                {"帧缓冲附件", IM_COL32(154, 92, 208, 255)}, {"其他", IM_COL32(85, 85, 85, 255)},
            };
            static_assert((size_t)GpuMemCategory::Count == 6, "分类样式表与枚举不同步");
            return kStyles[(size_t)cat];
        }

        std::string WithSeparators(uint64_t value)
        {
            std::string raw = std::to_string(value);
            std::string out;
            out.reserve(raw.size() + raw.size() / 3);
            for (size_t i = 0; i < raw.size(); ++i)
            {
                out += raw[i];
                size_t remain = raw.size() - i - 1;
                if (remain > 0 && remain % 3 == 0)
                    out += ',';
            }
            return out;
        }

        // 输入 MB/s，按量级自适应单位（对应模拟稿 fmtRate）
        std::string FormatRateMBps(float mbps)
        {
            std::ostringstream oss;
            oss << std::fixed;
            if (mbps >= 1024.0f)
            {
                oss.precision(2);
                oss << mbps / 1024.0f << " GB/s";
            }
            else if (mbps >= 1.0f)
            {
                oss.precision(1);
                oss << mbps << " MB/s";
            }
            else
            {
                oss.precision(0);
                oss << mbps * 1024.0f << " KB/s";
            }
            return oss.str();
        }

#ifdef _WIN32

        // 与 Win32 PROCESS_MEMORY_COUNTERS 布局一致，避免引入 psapi.h
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

        // 返回 false 表示查询失败；workingSet 单独有效时也填好 totalPhys
        void QueryCpuMemory(uint64_t& workingSetBytes, uint64_t& totalPhysBytes, bool& hasWorkingSet)
        {
            workingSetBytes = 0;
            totalPhysBytes  = 0;
            hasWorkingSet   = false;

            MEMORYSTATUSEX status{};
            status.dwLength = sizeof(status);
            if (GlobalMemoryStatusEx(&status))
                totalPhysBytes = status.ullTotalPhys;

            using K32Fn        = BOOL (*)(HANDLE, ProcessMemoryCountersLocal*, DWORD);
            static K32Fn fn    = nullptr;
            static bool  tried = false;
            if (!tried)
            {
                tried = true;
                fn    = reinterpret_cast<K32Fn>(reinterpret_cast<void*>(
                    ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo")));
                if (!fn)
                {
                    HMODULE psapi = ::LoadLibraryA("psapi.dll");
                    if (psapi)
                        fn = reinterpret_cast<K32Fn>(::GetProcAddress(psapi, "GetProcessMemoryInfo"));
                }
            }

            if (fn)
            {
                ProcessMemoryCountersLocal pmc{};
                pmc.cb = sizeof(pmc);
                if (fn(::GetCurrentProcess(), &pmc, sizeof(pmc)))
                {
                    workingSetBytes = pmc.WorkingSetSize;
                    hasWorkingSet   = true;
                }
            }
        }

#endif // _WIN32

    } // namespace

    void MemoryStatsPanel::OnImGuiRender(bool* open)
    {
        SampleBandwidth();

        const auto entries = GpuMemoryStats::Get().CollectLiveEntries();

        uint64_t catBytes[(size_t)GpuMemCategory::Count] = {};
        uint32_t catCount[(size_t)GpuMemCategory::Count] = {};
        uint64_t residentTriangles                       = 0;
        for (const auto& e : entries)
        {
            catBytes[(size_t)e.Category] += e.Bytes;
            catCount[(size_t)e.Category]++;
            residentTriangles += e.Triangles;
        }

        if (!ImGui::Begin("显存与内存监控", open))
        {
            ImGui::End();
            return;
        }

        if (ImGui::CollapsingHeader("占用总览", ImGuiTreeNodeFlags_DefaultOpen))
            DrawOverview(catBytes, catCount);

        DrawGpuStatus(); // NVML 不可用时内部整节隐藏

        if (ImGui::CollapsingHeader("GPU↔CPU 传输带宽", ImGuiTreeNodeFlags_DefaultOpen))
            DrawBandwidth();

        if (ImGui::CollapsingHeader("绘制统计", ImGuiTreeNodeFlags_DefaultOpen))
            DrawRenderStats(residentTriangles);

        if (ImGui::CollapsingHeader("资源明细 Top 5"))
            DrawTopResources(entries);

        ImGui::End();
    }

    void MemoryStatsPanel::SampleBandwidth()
    {
        auto& gmem = GpuMemoryStats::Get();

        const auto now      = std::chrono::steady_clock::now();
        const bool firstRun = m_LastSampleTime.time_since_epoch().count() == 0;
        if (firstRun)
        {
            m_LastSampleTime      = now;
            m_LastUploadedBytes   = gmem.GetUploadedBytes();
            m_LastDownloadedBytes = gmem.GetDownloadedBytes();
            return;
        }

        const float dtMs = std::chrono::duration<float, std::milli>(now - m_LastSampleTime).count();
        if (dtMs < kBandwidthSampleMs)
            return;

        const uint64_t uploaded   = gmem.GetUploadedBytes();
        const uint64_t downloaded = gmem.GetDownloadedBytes();
        const double   scale      = 1000.0 / dtMs / (1024.0 * 1024.0); // 字节差分 → MB/s
        const float    upMBps     = static_cast<float>(double(uploaded - m_LastUploadedBytes) * scale);
        const float    downMBps   = static_cast<float>(double(downloaded - m_LastDownloadedBytes) * scale);

        m_PeakUpMBps   = std::max(m_PeakUpMBps, upMBps);
        m_PeakDownMBps = std::max(m_PeakDownMBps, downMBps);

        m_UpHistoryMBps.push_back(upMBps);
        m_DownHistoryMBps.push_back(downMBps);
        while (m_UpHistoryMBps.size() > kBandwidthHistoryMax)
            m_UpHistoryMBps.erase(m_UpHistoryMBps.begin());
        while (m_DownHistoryMBps.size() > kBandwidthHistoryMax)
            m_DownHistoryMBps.erase(m_DownHistoryMBps.begin());

        m_LastSampleTime      = now;
        m_LastUploadedBytes   = uploaded;
        m_LastDownloadedBytes = downloaded;
    }

    void MemoryStatsPanel::DrawOverview(const uint64_t* catBytes, const uint32_t* catCounts)
    {
        uint64_t total = 0;
        for (size_t i = 0; i < (size_t)GpuMemCategory::Count; ++i)
            total += catBytes[i];

        // ---- GPU 侧标签行 ----
        const std::string totalLabel = "合计 " + GpuMemFormatBytes(total);
        ImGui::TextUnformatted("GPU 侧 · 引擎分配的显存");
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(totalLabel.c_str()).x);
        ImGui::TextUnformatted(totalLabel.c_str());

        // ---- 单一堆叠块 ----
        const float  barWidth  = ImGui::GetContentRegionAvail().x;
        const float  barHeight = 30.0f;
        const ImVec2 origin    = ImGui::GetCursorScreenPos();
        ImDrawList*  dl        = ImGui::GetWindowDrawList();

        float  x        = origin.x;
        size_t hoverCat = (size_t)GpuMemCategory::Count;
        for (size_t i = 0; i < (size_t)GpuMemCategory::Count; ++i)
        {
            if (catBytes[i] == 0 || total == 0)
                continue;
            const float frac = float(catBytes[i]) / float(total);
            float       segW = std::max(2.0f, barWidth * frac);
            segW             = std::min(segW, origin.x + barWidth - x);
            if (segW <= 0.0f)
                break;

            const auto& style = CatStyleOf((GpuMemCategory)i);
            dl->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + segW, origin.y + barHeight), style.Color);
            if (frac >= 0.08f)
            {
                char pct[8];
                std::snprintf(pct, sizeof(pct), "%.0f%%", frac * 100.0f);
                const ImVec2 ts = ImGui::CalcTextSize(pct);
                dl->AddText(ImVec2(x + segW * 0.5f - ts.x * 0.5f, origin.y + barHeight * 0.5f - ts.y * 0.5f),
                            IM_COL32(255, 255, 255, 255), pct);
            }
            x += segW;
        }
        dl->AddRect(origin, ImVec2(origin.x + barWidth, origin.y + barHeight), IM_COL32(31, 31, 31, 255));
        ImGui::Dummy(ImVec2(barWidth, barHeight));

        if (ImGui::IsItemHovered())
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            uint64_t     acc   = 0;
            for (size_t i = 0; i < (size_t)GpuMemCategory::Count; ++i)
            {
                acc += catBytes[i];
                if (acc == 0 || total == 0)
                    continue;
                if (mouse.x <= origin.x + barWidth * (float(acc) / float(total)))
                {
                    hoverCat = i;
                    break;
                }
            }
            if (hoverCat != (size_t)GpuMemCategory::Count)
            {
                const auto& style = CatStyleOf((GpuMemCategory)hoverCat);
                ImGui::SetTooltip("%s\n%s · %u 个 · %.1f%%", style.Name, GpuMemFormatBytes(catBytes[hoverCat]).c_str(),
                                  catCounts[hoverCat],
                                  total ? float(catBytes[hoverCat]) / float(total) * 100.0f : 0.0f);
            }
        }

        // ---- 图例 ----
        if (ImGui::BeginTable("##gmem_legend", 4, ImGuiTableFlags_NoBordersInBody))
        {
            ImGui::TableSetupColumn("分类", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("大小", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("个数", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("占比", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            for (size_t i = 0; i < (size_t)GpuMemCategory::Count; ++i)
            {
                if (catCounts[i] == 0)
                    continue;
                const auto& style = CatStyleOf((GpuMemCategory)i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const ImVec2 sp = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(sp, ImVec2(sp.x + 10, sp.y + ImGui::GetTextLineHeight()), style.Color);
                ImGui::Dummy(ImVec2(14, ImGui::GetTextLineHeight()));
                ImGui::SameLine();
                ImGui::TextUnformatted(style.Name);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(GpuMemFormatBytes(catBytes[i]).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u 个", catCounts[i]);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", total ? float(catBytes[i]) / float(total) * 100.0f : 0.0f);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("引擎估算值（含 mip），不含驱动对齐开销 · 悬停色块查看明细");

        // ---- CPU 侧 ----
#ifdef _WIN32
        uint64_t workingSet = 0, totalPhys = 0;
        bool     hasWorkingSet = false;
        QueryCpuMemory(workingSet, totalPhys, hasWorkingSet);

        ImGui::Spacing();
        ImGui::TextUnformatted("CPU 侧 · 进程工作集");
        const std::string wsLabel = hasWorkingSet ? GpuMemFormatBytes(workingSet) : "—";
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(wsLabel.c_str()).x);
        ImGui::TextUnformatted(wsLabel.c_str());

        const float  cpuWidth  = ImGui::GetContentRegionAvail().x;
        const ImVec2 cpuOrigin = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(cpuOrigin, ImVec2(cpuOrigin.x + cpuWidth, cpuOrigin.y + 12.0f), IM_COL32(74, 122, 154, 255));
        dl->AddRect(cpuOrigin, ImVec2(cpuOrigin.x + cpuWidth, cpuOrigin.y + 12.0f), IM_COL32(31, 31, 31, 255));
        ImGui::Dummy(ImVec2(cpuWidth, 12.0f));

        if (totalPhys > 0)
            ImGui::TextDisabled("物理内存 %.1f GB · 暂不分类统计", double(totalPhys) / (1024.0 * 1024 * 1024));
#else
        ImGui::TextDisabled("当前平台不支持系统内存查询");
#endif
    }

    void MemoryStatsPanel::DrawGpuStatus()
    {
        auto& nvml = NvmlSampler::Get();
        if (!m_NvmlTried)
        {
            m_NvmlTried = true;
            nvml.Init();
        }
        if (!nvml.IsAvailable())
            return; // 非 N 卡：整节隐藏

        // ~500ms 节流采样
        static std::chrono::steady_clock::time_point lastSample{};
        const auto                                   now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSample).count() > 500 ||
            lastSample == std::chrono::steady_clock::time_point{})
        {
            lastSample = now;
            nvml.Sample();
        }

        if (!ImGui::CollapsingHeader("GPU 状态", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::Columns(3, nullptr, false);
        ImGui::Text("%.0f°C", nvml.GetTempC());
        ImGui::TextDisabled("温度");
        ImGui::NextColumn();
        ImGui::Text("%.0f W", nvml.GetPowerW());
        ImGui::TextDisabled("功耗");
        ImGui::NextColumn();
        ImGui::Text("%.0f%%", nvml.GetGpuUtilPct());
        ImGui::TextDisabled("利用率");
        ImGui::Columns(1);
        ImGui::ProgressBar(nvml.GetGpuUtilPct() / 100.0f, ImVec2(-1, 4.0f), "");
        if (!nvml.GetDeviceName().empty())
            ImGui::TextDisabled("%s", nvml.GetDeviceName().c_str());
        ImGui::TextDisabled("数据来源 NVML · 非 N 卡自动隐藏本节");
    }

    void MemoryStatsPanel::DrawBandwidth()
    {
        auto row = [](const char* name, float rate, float peak)
        {
            ImGui::Text("%s %s", name, FormatRateMBps(rate).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("· 峰值 %s", FormatRateMBps(peak).c_str());
        };

        const bool  hasUp   = !m_UpHistoryMBps.empty();
        const float curUp   = hasUp ? m_UpHistoryMBps.back() : 0.0f;
        const float curDown = !m_DownHistoryMBps.empty() ? m_DownHistoryMBps.back() : 0.0f;
        row("上传:", curUp, m_PeakUpMBps);
        row("下载:", curDown, m_PeakDownMBps);

        if (hasUp)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.04f, 0.04f, 0.04f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.29f, 0.62f, 1.0f, 1.0f));
            ImGui::PlotLines("##bw_up", m_UpHistoryMBps.data(), (int)m_UpHistoryMBps.size(), 0, nullptr, FLT_MAX,
                             FLT_MAX, ImVec2(-1, 46.0f));
            ImGui::PopStyleColor();
            if (!m_DownHistoryMBps.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.95f, 0.44f, 0.11f, 1.0f));
                ImGui::PlotLines("##bw_down", m_DownHistoryMBps.data(), (int)m_DownHistoryMBps.size(), 0, nullptr,
                                 FLT_MAX, FLT_MAX, ImVec2(-1, 46.0f));
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleColor();
        }
        ImGui::TextDisabled("200ms 采样实时速率（1s 窗口差分）· 尖峰来自资源加载 / 回读突发");
    }

    void MemoryStatsPanel::DrawRenderStats(uint64_t residentTriangles)
    {
        const auto& stats = PerformanceMonitor::Get().GetStats();

        ImGui::Text("每帧三角面 %s", WithSeparators(stats.Triangles).c_str());
        ImGui::Text("绘制调用   %s", WithSeparators(stats.DrawCalls).c_str());
        ImGui::Text("驻留三角面 %s", WithSeparators(residentTriangles).c_str());
        ImGui::Text("粒子数     %s", WithSeparators(GpuMemoryStats::Get().GetParticleCount()).c_str());
        ImGui::TextDisabled("粒子经间接绘制提交，不计入每帧三角面");
    }

    void MemoryStatsPanel::DrawTopResources(const std::vector<GpuMemEntrySnapshot>& entries)
    {
        std::vector<const GpuMemEntrySnapshot*> sorted;
        sorted.reserve(entries.size());
        for (const auto& e : entries)
            sorted.push_back(&e);
        std::sort(sorted.begin(), sorted.end(),
                  [](const GpuMemEntrySnapshot* a, const GpuMemEntrySnapshot* b) { return a->Bytes > b->Bytes; });

        if (ImGui::BeginTable("##gmem_top", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableSetupColumn("资源", ImGuiTableColumnFlags_WidthStretch, 4.0f);
            ImGui::TableSetupColumn("分类", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("大小", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            const size_t count = std::min<size_t>(sorted.size(), 5);
            for (size_t i = 0; i < count; ++i)
            {
                const auto* e     = sorted[i];
                const auto& style = CatStyleOf(e->Category);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(style.Color));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("■");
                ImGui::SameLine();
                ImGui::PopStyleColor();
                ImGui::TextUnformatted(e->Label.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(style.Name);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(GpuMemFormatBytes(e->Bytes).c_str());
            }
            ImGui::EndTable();
        }
        if (entries.empty())
            ImGui::TextDisabled("暂无存活资源记录");
    }

} // namespace Engine
