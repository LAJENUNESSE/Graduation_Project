#pragma once

#include <string>

namespace Engine
{

    // NVIDIA Management Library 动态加载采样器。
    // nvml.dll 随 NVIDIA 驱动安装在系统目录；不可用（非 N 卡 / 无驱动）时
    // IsAvailable() == false，编辑器面板据此隐藏 GPU 状态区块。
    class NvmlSampler
    {
    public:
        static NvmlSampler& Get();

        NvmlSampler(const NvmlSampler&)            = delete;
        NvmlSampler& operator=(const NvmlSampler&) = delete;

        // 幂等；首次失败后本会话内不再重试
        bool Init();
        void Sample(); // 刷新温度/功耗/利用率（面板节流调用）

        bool               IsAvailable() const { return m_Available; }
        float              GetTempC() const { return m_TempC; }
        float              GetPowerW() const { return m_PowerW; }
        float              GetGpuUtilPct() const { return m_GpuUtilPct; }
        const std::string& GetDeviceName() const { return m_DeviceName; }

    private:
        NvmlSampler() = default;

        void*       m_Lib          = nullptr;
        void*       m_Device       = nullptr;
        bool        m_InitAttempted = false;
        bool        m_Available     = false;
        float       m_TempC         = 0.0f;
        float       m_PowerW        = 0.0f;
        float       m_GpuUtilPct    = 0.0f;
        std::string m_DeviceName;
    };

} // namespace Engine
