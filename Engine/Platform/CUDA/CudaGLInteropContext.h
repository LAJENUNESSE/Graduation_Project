#pragma once

// CUDA-OpenGL 互操作资源管理器。
//
// 拥有：cudaGraphicsResource 注册、CUDA 流、设备选择。
// 不拥有：它引用的底层 GL 缓冲区对象。
//
// 生命周期：必须在其引用的 GL 缓冲区之前被销毁。
// 线程：所有方法必须从 GL 上下文线程调用。

#include "Core/Base.h"
#include <cstdint>

namespace Engine
{

    class CudaGLInteropContext
    {
    public:
        // 探测当前 GL 上下文的设备是否支持 CUDA 互操作。
        // 在构建实例之前调用是安全的。
        static bool ProbeDeviceMatch();

        CudaGLInteropContext();
        ~CudaGLInteropContext();

        // 非可复制，非可移动（资源与设备/上下文相关）
        CudaGLInteropContext(const CudaGLInteropContext&) = delete;
        CudaGLInteropContext& operator=(const CudaGLInteropContext&) = delete;

        // 为 CUDA 互操作注册 GL 缓冲区。
        // 使用 cudaGraphicsRegisterFlagsNone（CUDA 可读写）。
        // 成功返回槽索引 (>= 0)，失败返回 -1。
        int RegisterBuffer(uint32_t glBufferID, const char* debugName);

        // 注销所有先前注册的缓冲区。
        void UnregisterAll();

        // 映射所有注册的缓冲区供 CUDA 访问。
        // 映射时 GL 不能访问已注册的缓冲区。
        // 失败返回 false（调用者应永久回退）。
        bool MapAll();

        // 取消映射所有缓冲区，将其返回给 GL。
        // 正确性保证：所有提交到 GetStream() 的 CUDA 工作
        // 在 GL 恢复访问权限之前完成（符合 NVIDIA 互操作规范）。
        void UnmapAll();

        // 已注册槽的设备指针。仅在 MapAll/UnmapAll 之间有效。
        void* GetMappedPointer(int slot) const;

        // 不透明流句柄——在 .cu 代码中强制转换为 cudaStream_t。
        void* GetStream() const;

        bool IsMapped() const;
        int  GetSlotCount() const;

    private:
        struct Impl;
        Scope<Impl> m_Impl;
    };

} // namespace Engine
