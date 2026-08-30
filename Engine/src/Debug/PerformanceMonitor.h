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
            m_RefreshHz             = refreshHz;
            m_RefreshPeriodMs       = refreshPeriodMs;
            m_SwapMissedVBlank      = swapMissedVBlank;
            m_SwapBurstId           = swapBurstId;
            m_SwapBurstLen          = swapBurstLen;
            m_SwapBurstMaxMs        = swapBurstMaxMs;
            m_SwapBurstMissedVBlank = swapBurstMissedVBlank;
            m_SwapBurstActive       = swapBurstActive;
        }

        // GPU timer queries (owned by monitor, used by Scene)
        GPUTimerQuery& GetShadowPassGPUTimer() { return *m_ShadowPassGPU; }
        GPUTimerQuery& GetSceneRenderGPUTimer() { return *m_SceneRenderGPU; }
        GPUTimerQuery& GetParticleComputeGPUTimer() { return *m_ParticleComputeGPU; }

        // Fluid compute timing
        GPUTimerQuery& GetFluidComputeGPUTimer() { return *m_FluidComputeGPU; }
        void           SetFluidActive(bool v) { m_FluidActive = v; }
        bool           IsFluidActive() const { return m_FluidActive; }

        // CUDA compute timing（由 ParticleSystemGPU/FluidSystemGPU 在 CUDA 路径下
        // 通过 CudaTimingHelper event query 喂入——与 GL timer 一致采用 1 帧延迟；
        // 未就绪时调用方保留上一帧缓存值）
        void  SetParticleComputeCudaMs(float ms) { m_ParticleComputeCudaMs = ms; }
        void  SetFluidComputeCudaMs(float ms) { m_FluidComputeCudaMs = ms; }
        float GetParticleComputeCudaMs() const { return m_ParticleComputeCudaMs; }
        float GetFluidComputeCudaMs() const { return m_FluidComputeCudaMs; }
        bool  IsParticleComputeCudaActive() const { return m_ParticleComputeCudaActive; }
        void  SetParticleComputeCudaActive(bool v) { m_ParticleComputeCudaActive = v; }
        bool  IsFluidComputeCudaActive() const { return m_FluidComputeCudaActive; }
        void  SetFluidComputeCudaActive(bool v) { m_FluidComputeCudaActive = v; }

        // Render stats (modified by RenderCommand::DrawIndexed)
        RenderStats&       GetStats() { return m_Stats; }
        const RenderStats& GetStats() const { return m_Stats; }

        // Accessors for ImGui panel
        float       GetFPS() const { return m_FPS; }
        float       GetFrameTimeMs() const { return m_FrameTimeMs; }
        float       GetShadowPassCpuMs() const { return m_ShadowPassCpuMs; }
        float       GetSceneRenderCpuMs() const { return m_SceneRenderCpuMs; }
        float       GetImGuiCpuMs() const { return m_ImGuiCpuMs; }
        float       GetPollEventsCpuMs() const { return m_PollEventsCpuMs; }
        float       GetSwapBuffersCpuMs() const { return m_SwapBuffersCpuMs; }
        float       GetRefreshHz() const { return m_RefreshHz; }
        float       GetRefreshPeriodMs() const { return m_RefreshPeriodMs; }
        uint32_t    GetSwapMissedVBlank() const { return m_SwapMissedVBlank; }
        uint32_t    GetSwapBurstId() const { return m_SwapBurstId; }
        uint32_t    GetSwapBurstLen() const { return m_SwapBurstLen; }
        float       GetSwapBurstMaxMs() const { return m_SwapBurstMaxMs; }
        uint32_t    GetSwapBurstMissedVBlank() const { return m_SwapBurstMissedVBlank; }
        bool        IsSwapBurstActive() const { return m_SwapBurstActive; }
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
        float GetShadowPassGpuMs() const { return m_ShadowPassGPU->GetElapsedMs(); }
        float GetSceneRenderGpuMs() const { return m_SceneRenderGPU->GetElapsedMs(); }
        float GetParticleComputeGpuMs() const { return m_ParticleComputeGPU->GetElapsedMs(); }
        float GetFluidComputeGpuMs() const { return m_FluidComputeGPU->GetElapsedMs(); }

        // Frame time history (ring buffer for PlotLines)
        static constexpr int FrameHistorySize = 120;
        const float*         GetFrameTimeHistory() const { return m_FrameTimeHistory; }
        int                  GetFrameTimeHistoryOffset() const { return m_FrameTimeHistoryOffset; }

    private:
        PerformanceMonitor();
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

        // GPU timers（按 RendererAPI 工厂分派的后端实现，构造时创建）
        Ref<GPUTimerQuery> m_ShadowPassGPU;
        Ref<GPUTimerQuery> m_SceneRenderGPU;
        Ref<GPUTimerQuery> m_ParticleComputeGPU;

        // Fluid GPU timers
        Ref<GPUTimerQuery> m_FluidComputeGPU;
        bool               m_FluidActive = false;

        // CUDA compute timing channels（与 GL timer 平行，独立通过 cudaEvent query 喂入）
        float m_ParticleComputeCudaMs     = 0.0f;
        float m_FluidComputeCudaMs        = 0.0f;
        bool  m_ParticleComputeCudaActive = false;
        bool  m_FluidComputeCudaActive    = false;

        // Render stats
        RenderStats m_Stats;

        // Frame time history ring buffer
        float m_FrameTimeHistory[FrameHistorySize] = {};
        int   m_FrameTimeHistoryOffset             = 0;

        // High-resolution clock for accurate frame timing
        std::chrono::high_resolution_clock::time_point m_FrameStartClock;
    };

} // namespace Engine
