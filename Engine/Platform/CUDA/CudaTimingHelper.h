#pragma once

// CUDA event 计时 ping-pong 双缓冲辅助（避免 GPU 同步阻塞）
// 纯 C++ 头——不暴露 CUDA 类型，void* 代表 cudaEvent_t。
//
// 使用模式（在 CUDA 工作段的两端 RecordStart / RecordStop）：
//   helper.RecordStart(stream);
//   /* CUDA 内核分派 */
//   helper.RecordStop(stream);
//   /* 帧末：helper.SwapAndQueryPrevious(previousMs) 读取上一帧已完成的耗时 */
//
// 上一帧 ms 通过非阻塞 query 获取；事件未完成时返回 -1（调用方应保留上一帧缓存值）。

namespace Engine
{

    struct CudaTimingHelper
    {
        void* EventStart       = nullptr;
        void* EventStop        = nullptr;
        void* PrevStart        = nullptr;
        void* PrevStop         = nullptr;
        bool  HasPrevTiming    = false;
        bool  BlockingReadback = false;

        void Init();
        void Destroy();

        // 在 CUDA 工作段开始时记录 start 事件。stream 为 cudaStream_t。
        void RecordStart(void* stream);
        // 在 CUDA 工作段结束时记录 stop 事件。stream 必须与 RecordStart 一致。
        void RecordStop(void* stream);

        // 把当前帧的 start/stop 转为 prev，prev 转为当前可用 slot。每帧调用一次。
        // 在 RecordStop 之后调用——下一帧 GetPrevElapsedMs() 才能读到这帧的结果。
        void SwapEvents();

        // 仅供离线 benchmark 使用；普通编辑器必须保持非阻塞。
        void SetBlockingReadback(bool enabled) { BlockingReadback = enabled; }

        // 读上一帧已完成的 CUDA 段耗时（毫秒）。
        // stop 事件未完成时返回 -1（调用方应保留上一帧缓存值）。
        float GetPrevElapsedMs() const;
    };

} // namespace Engine
