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
        uint32_t Vertices  = 0;
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
        void SetPresentDiagnostics(float    refreshHz,
                                   float    refreshPeriodMs,
                                   uint32_t swapMissedVBlank,
                                   uint32_t swapBurstId,
                                   uint32_t swapBurstLen,
                                   float    swapBurstMaxMs,
                                   uint32_t swapBurstMissedVBlank,
                                   bool     swapBurstActive)
        {
            m_RefreshHz                 = refreshHz;
            m_RefreshPeriodMs           = refreshPeriodMs;
            m_SwapMissedVBlank          = swapMissedVBlank;
            m_SwapBurstId               = swapBurstId;
            m_SwapBurstLen              = swapBurstLen;
            m_SwapBurstMaxMs            = swapBurstMaxMs;
            m_SwapBurstMissedVBlank     = swapBurstMissedVBlank;
            m_SwapBurstActive           = swapBurstActive;
        }

        // GPU timer queries (owned by monitor, used by Scene)
        GPUTimerQuery& GetShadowPassGPUTimer() { return m_ShadowPassGPU; }
        GPUTimerQuery& GetSceneRenderGPUTimer() { return m_SceneRenderGPU; }
        GPUTimerQuery& GetParticleComputeGPUTimer() { return m_ParticleComputeGPU; }

        // Particle compute timing (CUDA path sets result directly)
        void SetParticleComputeCudaMs(float ms) { m_ParticleComputeCudaMs = ms; }
        void SetParticleUsingCuda(bool v) { m_ParticleUsingCuda = m_ParticleUsingCuda || v; }
        void AddParticleInteropCpuTimings(float cudaMapAllMs, float cudaUnmapAllMs, float counterReadbackMs)
        {
            m_ParticleCudaMapAllCpuMs += cudaMapAllMs;
            m_ParticleCudaUnmapAllCpuMs += cudaUnmapAllMs;
            m_ParticleCounterReadbackCpuMs += counterReadbackMs;
        }
        void SetParticleABDiagnostics(bool forceGL, bool disableReadback)
        {
            m_ParticleABForceGL          = forceGL;
            m_ParticleABDisableReadback  = disableReadback;
        }
        bool IsParticleUsingCuda() const { return m_ParticleUsingCuda; }

        // Fluid compute timing
        GPUTimerQuery& GetFluidComputeGPUTimer() { return m_FluidComputeGPU; }
        void           SetFluidComputeCudaMs(float ms) { m_FluidComputeCudaMs = ms; }
        void           SetFluidUsingCuda(bool v) { m_FluidUsingCuda = v; }
        bool           IsFluidUsingCuda() const { return m_FluidUsingCuda; }
        void           SetFluidActive(bool v) { m_FluidActive = v; }
        bool           IsFluidActive() const { return m_FluidActive; }

        // Render stats (modified by RenderCommand::DrawIndexed)
        RenderStats&       GetStats() { return m_Stats; }
        const RenderStats& GetStats() const { return m_Stats; }

        // Accessors for ImGui panel
        float GetFPS() const { return m_FPS; }
        float GetFrameTimeMs() const { return m_FrameTimeMs; }
        float GetShadowPassCpuMs() const { return m_ShadowPassCpuMs; }
        float GetSceneRenderCpuMs() const { return m_SceneRenderCpuMs; }
        float GetImGuiCpuMs() const { return m_ImGuiCpuMs; }
        float GetPollEventsCpuMs() const { return m_PollEventsCpuMs; }
        float GetSwapBuffersCpuMs() const { return m_SwapBuffersCpuMs; }
        float GetRefreshHz() const { return m_RefreshHz; }
        float GetRefreshPeriodMs() const { return m_RefreshPeriodMs; }
        uint32_t GetSwapMissedVBlank() const { return m_SwapMissedVBlank; }
        uint32_t GetSwapBurstId() const { return m_SwapBurstId; }
        uint32_t GetSwapBurstLen() const { return m_SwapBurstLen; }
        float    GetSwapBurstMaxMs() const { return m_SwapBurstMaxMs; }
        uint32_t GetSwapBurstMissedVBlank() const { return m_SwapBurstMissedVBlank; }
        bool     IsSwapBurstActive() const { return m_SwapBurstActive; }
        const char* GetFrameDominantStageLabel() const
        {
            switch (m_FrameDominantStage)
            {
            case FrameDominantStage::Swap:
                return "Swap";
            case FrameDominantStage::Scene:
                return "Scene";
            case FrameDominantStage::ImGui:
                return "ImGui";
            case FrameDominantStage::PollEvents:
                return "PollEvents";
            case FrameDominantStage::Other:
            default:
                return "Other";
            }
        }
        float GetShadowPassGpuMs() const { return m_ShadowPassGPU.GetElapsedMs(); }
        float GetSceneRenderGpuMs() const { return m_SceneRenderGPU.GetElapsedMs(); }
        float GetParticleComputeGpuMs() const
        {
            return m_ParticleUsingCuda ? m_ParticleComputeCudaMs : m_ParticleComputeGPU.GetElapsedMs();
        }
        float GetParticleCudaMapAllCpuMs() const { return m_ParticleCudaMapAllCpuMs; }
        float GetParticleCudaUnmapAllCpuMs() const { return m_ParticleCudaUnmapAllCpuMs; }
        float GetParticleCounterReadbackCpuMs() const { return m_ParticleCounterReadbackCpuMs; }
        bool  IsParticleABForceGL() const { return m_ParticleABForceGL; }
        bool  IsParticleABDisableReadback() const { return m_ParticleABDisableReadback; }
        float GetFluidComputeGpuMs() const
        {
            return m_FluidUsingCuda ? m_FluidComputeCudaMs : m_FluidComputeGPU.GetElapsedMs();
        }

        // Frame time history (ring buffer for PlotLines)
        static constexpr int FrameHistorySize = 120;
        const float*         GetFrameTimeHistory() const { return m_FrameTimeHistory; }
        int                  GetFrameTimeHistoryOffset() const { return m_FrameTimeHistoryOffset; }

    private:
        PerformanceMonitor()                                     = default;
        ~PerformanceMonitor()                                    = default;
        PerformanceMonitor(const PerformanceMonitor&)            = delete;
        PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

        // CSV output
        std::ofstream m_CsvFile;
        uint32_t      m_FrameNumber  = 0;
        int           m_FlushCounter = 0;

        // Frame data
        float m_TimestampSeconds = 0.0f;
        float m_FrameTimeMs      = 0.0f;
        float m_FPS              = 0.0f;

        // CPU timings
        float m_ShadowPassCpuMs  = 0.0f;
        float m_SceneRenderCpuMs = 0.0f;
        float m_ImGuiCpuMs       = 0.0f;
        float m_PollEventsCpuMs  = 0.0f;
        float m_SwapBuffersCpuMs = 0.0f;

        enum class FrameDominantStage : uint8_t
        {
            Swap = 0,
            Scene,
            ImGui,
            PollEvents,
            Other
        };

        FrameDominantStage m_FrameDominantStage = FrameDominantStage::Other;

        // Present/Swap diagnostics
        float    m_RefreshHz             = 0.0f;
        float    m_RefreshPeriodMs       = 0.0f;
        uint32_t m_SwapMissedVBlank      = 0;
        uint32_t m_SwapBurstId           = 0;
        uint32_t m_SwapBurstLen          = 0;
        float    m_SwapBurstMaxMs        = 0.0f;
        uint32_t m_SwapBurstMissedVBlank = 0;
        bool     m_SwapBurstActive       = false;

        // GPU timers
        GPUTimerQuery m_ShadowPassGPU;
        GPUTimerQuery m_SceneRenderGPU;
        GPUTimerQuery m_ParticleComputeGPU;
        float         m_ParticleComputeCudaMs = 0.0f;
        bool          m_ParticleUsingCuda     = false;
        float         m_ParticleCudaMapAllCpuMs = 0.0f;
        float         m_ParticleCudaUnmapAllCpuMs = 0.0f;
        float         m_ParticleCounterReadbackCpuMs = 0.0f;
        bool          m_ParticleABForceGL = false;
        bool          m_ParticleABDisableReadback = false;

        // Fluid GPU timers
        GPUTimerQuery m_FluidComputeGPU;
        float         m_FluidComputeCudaMs = 0.0f;
        bool          m_FluidUsingCuda     = false;
        bool          m_FluidActive        = false;

        // Render stats
        RenderStats m_Stats;

        // Frame time history ring buffer
        float m_FrameTimeHistory[FrameHistorySize] = {};
        int   m_FrameTimeHistoryOffset             = 0;

        // High-resolution clock for accurate frame timing
        std::chrono::high_resolution_clock::time_point m_FrameStartClock;
    };

} // namespace Engine
