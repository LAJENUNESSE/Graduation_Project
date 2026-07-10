#pragma once

// CUDA event 计时 ping-pong 双缓冲辅助（避免 GPU 同步阻塞）
// 纯 C++ 头——不暴露 CUDA 类型，void* 代表 cudaEvent_t。

namespace Engine
{

    struct CudaTimingHelper
    {
        void* EventStart    = nullptr;
        void* EventStop     = nullptr;
        void* PrevStart     = nullptr;
        void* PrevStop      = nullptr;
        bool  HasPrevTiming = false;

        void Init();
        void SwapEvents();
        void Destroy();
    };

} // namespace Engine
