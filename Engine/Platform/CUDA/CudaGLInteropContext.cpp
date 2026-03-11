#include "engpch.h"
#include "Platform/CUDA/CudaGLInteropContext.h"
#include "Core/Log.h"

#ifdef _WIN32
#include <windows.h> // WINGDIAPI / APIENTRY — required before <GL/gl.h>
#endif
#include <cuda_gl_interop.h>
#include <cuda_runtime.h>

namespace Engine
{

    // ------------------------------------------------------------------ 辅助函数

    static const char* CudaErrStr(cudaError_t err)
    {
        return cudaGetErrorString(err);
    }

#define CUDA_LOG_ERR(call, err) ENGINE_CORE_ERROR("[CUDA] {0} failed: {1}", #call, CudaErrStr(err))

    // ------------------------------------------------------------------ 实现

    struct CudaGLInteropContext::Impl
    {
        struct Slot
        {
            cudaGraphicsResource_t resource = nullptr;
            void* mappedPtr = nullptr;
            std::string name;
        };

        std::vector<Slot> slots;
        cudaStream_t stream = nullptr;
        int deviceId = -1;
        bool mapped = false;
    };

    // ------------------------------------------------------------------ 静态

    bool CudaGLInteropContext::ProbeDeviceMatch()
    {
        unsigned int count = 0;
        int device = -1;
        cudaError_t err = cudaGLGetDevices(&count, &device, 1, cudaGLDeviceListAll);
        if (err != cudaSuccess || count == 0)
        {
            ENGINE_CORE_WARN("[CUDA] ProbeDeviceMatch: count={0}, err={1}", count, cudaGetErrorString(err));
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------ 构造/析构

    CudaGLInteropContext::CudaGLInteropContext() : m_Impl(CreateScope<Impl>())
    {
        unsigned int count = 0;
        int device = -1;
        cudaError_t err = cudaGLGetDevices(&count, &device, 1, cudaGLDeviceListAll);
        if (err != cudaSuccess || count == 0)
        {
            ENGINE_CORE_ERROR("[CUDA] No CUDA device matching GL context");
            return;
        }

        err = cudaSetDevice(device);
        if (err != cudaSuccess)
        {
            CUDA_LOG_ERR(cudaSetDevice, err);
            return;
        }
        m_Impl->deviceId = device;

        err = cudaStreamCreate(&m_Impl->stream);
        if (err != cudaSuccess)
        {
            CUDA_LOG_ERR(cudaStreamCreate, err);
            return;
        }

        cudaDeviceProp prop{};
        cudaGetDeviceProperties(&prop, device);
        ENGINE_CORE_INFO("[CUDA] Device {0}: {1} (SM {2}.{3})", device, prop.name, prop.major, prop.minor);
    }

    CudaGLInteropContext::~CudaGLInteropContext()
    {
        if (m_Impl->mapped)
            UnmapAll();
        UnregisterAll();
        if (m_Impl->stream)
        {
            cudaStreamDestroy(m_Impl->stream);
            m_Impl->stream = nullptr;
        }
    }

    // ------------------------------------------------------------------ 注册

    int CudaGLInteropContext::RegisterBuffer(uint32_t glBufferID, const char* debugName)
    {
        cudaGraphicsResource_t resource = nullptr;
        cudaError_t err = cudaGraphicsGLRegisterBuffer(&resource, glBufferID, cudaGraphicsRegisterFlagsNone);

        if (err != cudaSuccess)
        {
            ENGINE_CORE_ERROR("[CUDA] RegisterBuffer '{0}' (GL id={1}) failed: {2}", debugName, glBufferID,
                              CudaErrStr(err));
            return -1;
        }

        int slot = static_cast<int>(m_Impl->slots.size());
        m_Impl->slots.push_back({resource, nullptr, debugName ? debugName : ""});
        return slot;
    }

    void CudaGLInteropContext::UnregisterAll()
    {
        for (auto& s : m_Impl->slots)
        {
            if (s.resource)
            {
                cudaGraphicsUnregisterResource(s.resource);
                s.resource = nullptr;
            }
        }
        m_Impl->slots.clear();
    }

    // ------------------------------------------------------------------ 映射/取消映射

    bool CudaGLInteropContext::MapAll()
    {
        if (m_Impl->mapped || m_Impl->slots.empty())
            return false;

        int n = static_cast<int>(m_Impl->slots.size());
        std::vector<cudaGraphicsResource_t> res(n);
        for (int i = 0; i < n; i++)
            res[i] = m_Impl->slots[i].resource;

        cudaError_t err = cudaGraphicsMapResources(n, res.data(), m_Impl->stream);
        if (err != cudaSuccess)
        {
            ENGINE_CORE_ERROR("[CUDA] MapAll failed: {0}", CudaErrStr(err));
            return false;
        }

        for (int i = 0; i < n; i++)
        {
            size_t sz = 0;
            err = cudaGraphicsResourceGetMappedPointer(&m_Impl->slots[i].mappedPtr, &sz, res[i]);
            if (err != cudaSuccess)
            {
                ENGINE_CORE_ERROR("[CUDA] GetMappedPointer slot {0} ('{1}') failed: {2}", i, m_Impl->slots[i].name,
                                  CudaErrStr(err));
                cudaGraphicsUnmapResources(n, res.data(), m_Impl->stream);
                for (auto& s : m_Impl->slots)
                    s.mappedPtr = nullptr;
                return false;
            }
        }

        m_Impl->mapped = true;
        return true;
    }

    void CudaGLInteropContext::UnmapAll()
    {
        if (!m_Impl->mapped)
            return;

        // 调试同步——仅当 ENGINE_CUDA_DEBUG_SYNC=1 时。
        // 发布路径依赖 cudaGraphicsUnmapResources 的隐式同步。
        static const bool sDebugSync = []
        {
            const char* e = std::getenv("ENGINE_CUDA_DEBUG_SYNC");
            return e && e[0] == '1';
        }();
        if (sDebugSync && m_Impl->stream)
            cudaStreamSynchronize(m_Impl->stream);

        int n = static_cast<int>(m_Impl->slots.size());
        std::vector<cudaGraphicsResource_t> res(n);
        for (int i = 0; i < n; i++)
            res[i] = m_Impl->slots[i].resource;

        cudaGraphicsUnmapResources(n, res.data(), m_Impl->stream);

        for (auto& s : m_Impl->slots)
            s.mappedPtr = nullptr;
        m_Impl->mapped = false;
    }

    // ------------------------------------------------------------------ 访问器

    void* CudaGLInteropContext::GetMappedPointer(int slot) const
    {
        if (slot < 0 || slot >= static_cast<int>(m_Impl->slots.size()))
            return nullptr;
        return m_Impl->slots[slot].mappedPtr;
    }

    void* CudaGLInteropContext::GetStream() const
    {
        return m_Impl->stream;
    }
    bool CudaGLInteropContext::IsMapped() const
    {
        return m_Impl->mapped;
    }

    int CudaGLInteropContext::GetSlotCount() const
    {
        return static_cast<int>(m_Impl->slots.size());
    }

} // namespace Engine
