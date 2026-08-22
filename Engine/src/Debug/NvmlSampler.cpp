#include "engpch.h"
#include "Debug/NvmlSampler.h"

#include "Core/Log.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Engine
{

    typedef void* NvmlDeviceHandle;

    // 对应 nvmlUtilization { unsigned gpu; unsigned memory; }
    struct NvmlUtilization
    {
        unsigned gpu;
        unsigned memory;
    };

    using NvmlInitFn      = int (*)();
    using NvmlGetHandleFn = int (*)(unsigned, NvmlDeviceHandle*);
    using NvmlGetNameFn   = int (*)(NvmlDeviceHandle, char*, unsigned);
    using NvmlGetTempFn   = int (*)(NvmlDeviceHandle, int, unsigned*);
    using NvmlGetPowerFn  = int (*)(NvmlDeviceHandle, unsigned*);
    using NvmlGetUtilFn   = int (*)(NvmlDeviceHandle, NvmlUtilization*);

    // NVML_TEMPERATURE_GPU / NVML_SUCCESS
    static constexpr int      kNvmlTemperatureGpu = 0;
    static constexpr unsigned kNvmlSuccess         = 0;

    NvmlSampler& NvmlSampler::Get()
    {
        static NvmlSampler instance;
        return instance;
    }

    bool NvmlSampler::Init()
    {
        if (m_InitAttempted)
            return m_Available;
        m_InitAttempted = true;

        auto lib = ::LoadLibraryA("nvml.dll");
        if (!lib)
        {
            ENGINE_CORE_INFO("[NVML] nvml.dll 不可用，GPU 状态区块将隐藏");
            return false;
        }
        m_Lib = lib;

        auto initFn       = reinterpret_cast<NvmlInitFn>(::GetProcAddress(lib, "nvmlInit_v2"));
        auto getHandleFn  = reinterpret_cast<NvmlGetHandleFn>(::GetProcAddress(lib, "nvmlDeviceGetHandleByIndex_v2"));
        auto getNameFn    = reinterpret_cast<NvmlGetNameFn>(::GetProcAddress(lib, "nvmlDeviceGetName"));
        auto getTempFn    = reinterpret_cast<NvmlGetTempFn>(::GetProcAddress(lib, "nvmlDeviceGetTemperature"));
        auto getPowerFn   = reinterpret_cast<NvmlGetPowerFn>(::GetProcAddress(lib, "nvmlDeviceGetPowerUsage"));
        auto getUtilFn    = reinterpret_cast<NvmlGetUtilFn>(::GetProcAddress(lib, "nvmlDeviceGetUtilizationRates"));

        if (!initFn || !getHandleFn || !getTempFn || !getPowerFn || !getUtilFn)
        {
            ENGINE_CORE_WARN("[NVML] 导出函数缺失，GPU 状态区块将隐藏");
            return false;
        }

        if (initFn() != static_cast<int>(kNvmlSuccess))
        {
            ENGINE_CORE_WARN("[NVML] 初始化失败，GPU 状态区块将隐藏");
            return false;
        }

        if (getHandleFn(0, &m_Device) != static_cast<int>(kNvmlSuccess))
        {
            m_Device = nullptr;
            return false;
        }

        char name[96] = {};
        if (getNameFn && m_Device && getNameFn(m_Device, name, sizeof(name)) == static_cast<int>(kNvmlSuccess))
            m_DeviceName = name;

        m_Available = true;
        ENGINE_CORE_INFO("[NVML] 就绪: {}", m_DeviceName);
        Sample();
        return true;
    }

    void NvmlSampler::Sample()
    {
        if (!m_Available)
            Init();
        if (!m_Available || !m_Device)
            return;

        auto     lib     = static_cast<HMODULE>(m_Lib);
        auto     getTempFn  = reinterpret_cast<NvmlGetTempFn>(::GetProcAddress(lib, "nvmlDeviceGetTemperature"));
        auto     getPowerFn = reinterpret_cast<NvmlGetPowerFn>(::GetProcAddress(lib, "nvmlDeviceGetPowerUsage"));
        auto     getUtilFn  = reinterpret_cast<NvmlGetUtilFn>(::GetProcAddress(lib, "nvmlDeviceGetUtilizationRates"));
        auto     device     = static_cast<NvmlDeviceHandle>(m_Device);

        unsigned value = 0;
        if (getTempFn && getTempFn(device, kNvmlTemperatureGpu, &value) == static_cast<int>(kNvmlSuccess))
            m_TempC = static_cast<float>(value);

        if (getPowerFn && getPowerFn(device, &value) == static_cast<int>(kNvmlSuccess))
            m_PowerW = static_cast<float>(value) / 1000.0f; // 毫瓦 → 瓦

        NvmlUtilization util{};
        if (getUtilFn && getUtilFn(device, &util) == static_cast<int>(kNvmlSuccess))
            m_GpuUtilPct = static_cast<float>(util.gpu);
    }

} // namespace Engine

#else // !_WIN32

namespace Engine
{

    NvmlSampler& NvmlSampler::Get()
    {
        static NvmlSampler instance;
        return instance;
    }

    bool NvmlSampler::Init()
    {
        m_InitAttempted = true;
        return false;
    }

    void NvmlSampler::Sample() {}

} // namespace Engine

#endif // _WIN32
