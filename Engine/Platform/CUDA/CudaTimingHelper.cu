// CudaTimingHelper 实现
// 依赖 CudaInterop event 函数（定义在 CudaParticlePipeline.cu）

#include "Platform/CUDA/CudaTimingHelper.h"
#include "Platform/CUDA/CudaParticlePipeline.h"
#include "Platform/CUDA/CudaErrorHandling.h"

#include <algorithm>

namespace Engine
{

    void CudaTimingHelper::Init()
    {
        EventStart = CudaInterop::CreateCudaEvent();
        EventStop  = CudaInterop::CreateCudaEvent();
        PrevStart  = CudaInterop::CreateCudaEvent();
        PrevStop   = CudaInterop::CreateCudaEvent();
    }

    void CudaTimingHelper::Destroy()
    {
        if (EventStart)
            CudaInterop::DestroyCudaEvent(EventStart);
        if (EventStop)
            CudaInterop::DestroyCudaEvent(EventStop);
        if (PrevStart)
            CudaInterop::DestroyCudaEvent(PrevStart);
        if (PrevStop)
            CudaInterop::DestroyCudaEvent(PrevStop);
        EventStart = EventStop = PrevStart = PrevStop = nullptr;
        HasPrevTiming                                 = false;
    }

    void CudaTimingHelper::RecordStart(void* stream)
    {
        if (!EventStart || CudaInterop::IsCudaPoisoned())
            return;
        CudaInterop::RecordCudaEvent(EventStart, stream);
    }

    void CudaTimingHelper::RecordStop(void* stream)
    {
        if (!EventStop || CudaInterop::IsCudaPoisoned())
            return;
        CudaInterop::RecordCudaEvent(EventStop, stream);
    }

    void CudaTimingHelper::SwapEvents()
    {
        std::swap(EventStart, PrevStart);
        std::swap(EventStop, PrevStop);
        HasPrevTiming = true;
    }

    float CudaTimingHelper::GetPrevElapsedMs() const
    {
        if (!HasPrevTiming || !PrevStart || !PrevStop)
            return -1.0f;
        return CudaInterop::CudaEventElapsedMs(PrevStart, PrevStop, BlockingReadback);
    }

} // namespace Engine
