#include "engpch.h"
#include "Debug/PerformanceMonitor.h"
#include "Core/Log.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>

namespace Engine
{

    void PerformanceMonitor::Init()
    {
        // Create logs directory
        std::filesystem::create_directories("logs");

        // Generate filename: perf_YYYYMMDD_HHMMSS.csv
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _MSC_VER
        localtime_s(&tm, &time);    // MSVC: 参数顺序相反
#else
        localtime_r(&time, &tm);    // POSIX
#endif

        std::ostringstream filename;
        filename << "logs/perf_"
                 << std::put_time(&tm, "%Y%m%d_%H%M%S")
                 << ".csv";

        m_CsvFile.open(filename.str());
        if (m_CsvFile.is_open())
        {
            m_CsvFile << "Frame,Timestamp_s,FrameTime_ms,FPS,"
                      << "ShadowPass_CPU_ms,SceneRender_CPU_ms,ImGui_CPU_ms,"
                      << "ShadowPass_GPU_ms,SceneRender_GPU_ms,"
                      << "DrawCalls,Vertices,Triangles\n";
            ENGINE_CORE_INFO("Performance CSV: {}", filename.str());
        }
        else
        {
            ENGINE_CORE_WARN("Failed to open performance CSV: {}", filename.str());
        }

        m_FrameNumber = 0;
        m_FlushCounter = 0;
    }

    void PerformanceMonitor::Shutdown()
    {
        // Release GPU queries while GL context is still alive
        m_ShadowPassGPU.Destroy();
        m_SceneRenderGPU.Destroy();

        if (m_CsvFile.is_open())
        {
            m_CsvFile.flush();
            m_CsvFile.close();
            ENGINE_CORE_INFO("Performance CSV closed. Total frames: {}", m_FrameNumber);
        }
    }

    void PerformanceMonitor::BeginFrame(float timestampSeconds)
    {
        m_TimestampSeconds = timestampSeconds;
        m_FrameStartClock = std::chrono::high_resolution_clock::now();

        // Reset render stats
        m_Stats.DrawCalls = 0;
        m_Stats.Vertices = 0;
        m_Stats.Triangles = 0;
    }

    void PerformanceMonitor::EndFrame()
    {
        // Compute frame time at END so it aligns with CPU sub-timers
        auto now = std::chrono::high_resolution_clock::now();
        m_FrameTimeMs = std::chrono::duration<float, std::milli>(now - m_FrameStartClock).count();
        m_FPS = (m_FrameTimeMs > 0.0f) ? (1000.0f / m_FrameTimeMs) : 0.0f;

        // Update frame time history ring buffer
        m_FrameTimeHistory[m_FrameTimeHistoryOffset] = m_FrameTimeMs;
        m_FrameTimeHistoryOffset = (m_FrameTimeHistoryOffset + 1) % FrameHistorySize;

        m_FrameNumber++;

        // Write CSV row
        if (m_CsvFile.is_open())
        {
            m_CsvFile << m_FrameNumber << ","
                      << std::fixed << std::setprecision(4) << m_TimestampSeconds << ","
                      << std::setprecision(3) << m_FrameTimeMs << ","
                      << std::setprecision(1) << m_FPS << ","
                      << std::setprecision(3) << m_ShadowPassCpuMs << ","
                      << std::setprecision(3) << m_SceneRenderCpuMs << ","
                      << std::setprecision(3) << m_ImGuiCpuMs << ","
                      << std::setprecision(3) << m_ShadowPassGPU.GetElapsedMs() << ","
                      << std::setprecision(3) << m_SceneRenderGPU.GetElapsedMs() << ","
                      << std::defaultfloat
                      << m_Stats.DrawCalls << ","
                      << m_Stats.Vertices << ","
                      << m_Stats.Triangles << "\n";

            // Flush every 60 frames (~1 second at 60fps)
            m_FlushCounter++;
            if (m_FlushCounter >= 60)
            {
                m_CsvFile.flush();
                m_FlushCounter = 0;
            }
        }
    }

} // namespace Engine
