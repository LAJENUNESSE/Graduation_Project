#pragma once

#include "Core/Base.h"
#include "Debug/GPUTimerQuery.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>

namespace Engine
{

    struct RenderStats
    {
        uint32_t DrawCalls = 0;
        uint32_t Vertices = 0;
        uint32_t Triangles = 0;
    };

    class PerformanceMonitor
    {
    public:
        static PerformanceMonitor& Get()
        {
            static PerformanceMonitor instance;
            return instance;
        }

        void Init();
        void Shutdown();

        void BeginFrame(float timestampSeconds);
        void EndFrame();

        // CPU timing setters (called by PROFILE_SCOPE users)
        void SetShadowPassCPU(float ms) { m_ShadowPassCpuMs = ms; }
        void SetSceneRenderCPU(float ms) { m_SceneRenderCpuMs = ms; }
        void SetImGuiCPU(float ms) { m_ImGuiCpuMs = ms; }
        void SetPollEventsCPU(float ms) { m_PollEventsCpuMs = ms; }
        void SetSwapBuffersCPU(float ms) { m_SwapBuffersCpuMs = ms; }

        // GPU timer queries (owned by monitor, used by Scene)
        GPUTimerQuery& GetShadowPassGPUTimer() { return m_ShadowPassGPU; }
        GPUTimerQuery& GetSceneRenderGPUTimer() { return m_SceneRenderGPU; }
        GPUTimerQuery& GetParticleComputeGPUTimer() { return m_ParticleComputeGPU; }

        // Particle compute timing (CUDA path sets result directly)
        void SetParticleComputeCudaMs(float ms) { m_ParticleComputeCudaMs = ms; }
        void SetParticleUsingCuda(bool v) { m_ParticleUsingCuda = v; }
        bool IsParticleUsingCuda() const { return m_ParticleUsingCuda; }

        // Render stats (modified by RenderCommand::DrawIndexed)
        RenderStats& GetStats() { return m_Stats; }
        const RenderStats& GetStats() const { return m_Stats; }

        // Accessors for ImGui panel
        float GetFPS() const { return m_FPS; }
        float GetFrameTimeMs() const { return m_FrameTimeMs; }
        float GetShadowPassCpuMs() const { return m_ShadowPassCpuMs; }
        float GetSceneRenderCpuMs() const { return m_SceneRenderCpuMs; }
        float GetImGuiCpuMs() const { return m_ImGuiCpuMs; }
        float GetPollEventsCpuMs() const { return m_PollEventsCpuMs; }
        float GetSwapBuffersCpuMs() const { return m_SwapBuffersCpuMs; }
        float GetShadowPassGpuMs() const { return m_ShadowPassGPU.GetElapsedMs(); }
        float GetSceneRenderGpuMs() const { return m_SceneRenderGPU.GetElapsedMs(); }
        float GetParticleComputeGpuMs() const
        {
            return m_ParticleUsingCuda ? m_ParticleComputeCudaMs : m_ParticleComputeGPU.GetElapsedMs();
        }

        // Frame time history (ring buffer for PlotLines)
        static constexpr int FrameHistorySize = 120;
        const float* GetFrameTimeHistory() const { return m_FrameTimeHistory; }
        int GetFrameTimeHistoryOffset() const { return m_FrameTimeHistoryOffset; }

    private:
        PerformanceMonitor() = default;
        ~PerformanceMonitor() = default;
        PerformanceMonitor(const PerformanceMonitor&) = delete;
        PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

        // CSV output
        std::ofstream m_CsvFile;
        uint32_t m_FrameNumber = 0;
        int m_FlushCounter = 0;

        // Frame data
        float m_TimestampSeconds = 0.0f;
        float m_FrameTimeMs = 0.0f;
        float m_FPS = 0.0f;

        // CPU timings
        float m_ShadowPassCpuMs = 0.0f;
        float m_SceneRenderCpuMs = 0.0f;
        float m_ImGuiCpuMs = 0.0f;
        float m_PollEventsCpuMs = 0.0f;
        float m_SwapBuffersCpuMs = 0.0f;

        // GPU timers
        GPUTimerQuery m_ShadowPassGPU;
        GPUTimerQuery m_SceneRenderGPU;
        GPUTimerQuery m_ParticleComputeGPU;
        float m_ParticleComputeCudaMs = 0.0f;
        bool m_ParticleUsingCuda = false;

        // Render stats
        RenderStats m_Stats;

        // Frame time history ring buffer
        float m_FrameTimeHistory[FrameHistorySize] = {};
        int m_FrameTimeHistoryOffset = 0;

        // High-resolution clock for accurate frame timing
        std::chrono::high_resolution_clock::time_point m_FrameStartClock;
    };

} // namespace Engine
