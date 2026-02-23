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
        localtime_r(&time, &tm);

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

    void PerformanceMonitor::BeginFrame(float timestampSeconds, float frameTimeMs)
    {
        m_TimestampSeconds = timestampSeconds;
        m_FrameTimeMs = frameTimeMs;
        m_FPS = (frameTimeMs > 0.0f) ? (1000.0f / frameTimeMs) : 0.0f;

        // Reset render stats
        m_Stats.DrawCalls = 0;
        m_Stats.Vertices = 0;
        m_Stats.Triangles = 0;

        // Update frame time history ring buffer
        m_FrameTimeHistory[m_FrameTimeHistoryOffset] = frameTimeMs;
        m_FrameTimeHistoryOffset = (m_FrameTimeHistoryOffset + 1) % FrameHistorySize;
    }

    void PerformanceMonitor::EndFrame()
    {
        m_FrameNumber++;

        // Write CSV row
        if (m_CsvFile.is_open())
        {
            m_CsvFile << m_FrameNumber << ","
                      << std::fixed << std::setprecision(4) << m_TimestampSeconds << ","
                      << std::setprecision(3) << m_FrameTimeMs << ","
                      << std::setprecision(1) << m_FPS << ","
                      << std::setprecision(3) << m_ShadowPassCpuMs << ","
                      << m_SceneRenderCpuMs << ","
                      << m_ImGuiCpuMs << ","
                      << m_ShadowPassGPU.GetElapsedMs() << ","
                      << m_SceneRenderGPU.GetElapsedMs() << ","
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
