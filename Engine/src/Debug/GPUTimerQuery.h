#pragma once

#include <cstdint>

namespace Engine
{

    // Double-buffered GPU timer query to avoid pipeline stalls.
    // Frame N: Begin/End writes to query[current].
    // Frame N+1: Begin reads query[previous] result, then starts new query.
    // Result has 1-frame latency but zero stall.
    class GPUTimerQuery
    {
    public:
        GPUTimerQuery();
        ~GPUTimerQuery();

        // Release GL resources explicitly (must be called while GL context is alive).
        void Destroy();

        // Call Begin before the GPU work you want to measure.
        // Internally reads the previous frame's result first.
        void Begin();

        // Call End after the GPU work.
        void End();

        // Returns the GPU elapsed time in milliseconds (from the previous frame).
        float GetElapsedMs() const { return m_ElapsedMs; }

        // Non-copyable
        GPUTimerQuery(const GPUTimerQuery&)            = delete;
        GPUTimerQuery& operator=(const GPUTimerQuery&) = delete;

    private:
        uint32_t m_QueryIDs[2]  = {0, 0};
        int      m_CurrentIndex = 0;
        float    m_ElapsedMs    = 0.0f;
        bool     m_QueryActive  = false; // Is there an active query in End() waiting?
        int      m_FrameCount   = 0;     // Track frames to know when results are available
        bool     m_Disabled     = false; // Disabled on unstable drivers (e.g., VMware) or env override
    };

} // namespace Engine
