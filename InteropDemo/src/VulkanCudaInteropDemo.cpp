#include "CudaParticleKernel.h"
#include "Engine/Platform/CUDA/VulkanInteropCommon.h"

#include <cuda_runtime.h>

#ifdef _WIN32
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Reuse precompiled SPIR-V blobs shipped with Dear ImGui backend sources.
#include "vendor/imgui/backends/imgui_impl_sdlgpu3_shaders.h"

namespace
{
    using Engine::CudaInterop::BuildInteropFrameSyncValues;
    using Engine::CudaInterop::ExternalHandleType;
    using Engine::CudaInterop::GetDefaultExternalHandleType;
    using Engine::CudaInterop::InteropFrameSyncValues;
    using Engine::CudaInterop::OwnedInteropHandle;

    struct DemoConfig
    {
        uint32_t Particles        = 100000u;
        uint32_t PCISPHIterations = 3u;
        bool     Calibrate        = true;
        bool     VSync            = false;
        uint32_t DurationSec      = 180u;
    };

    struct FrameTelemetry
    {
        float    CudaWaitMs   = 0.0f;
        float    CudaKernelMs = 0.0f;
        float    CudaSignalMs = 0.0f;
        float    VkAcquireMs  = 0.0f;
        float    VkSubmitMs   = 0.0f;
        float    VkPresentMs  = 0.0f;
        float    CalibrationP95Ms = 0.0f;
        float    CalibrationMaxMs = 0.0f;
        uint32_t CalibrationSpikeCount = 0;
        uint64_t WaitVulkanValue = 0;
        uint64_t CudaSignalValue = 0;
        uint64_t VkSignalValue   = 0;
        bool     Success          = true;
        std::string Error;
    };

    struct CalibrationSummary
    {
        uint32_t    ParticleCount = 0;
        bool        Passed        = false;
        float       P95PresentMs  = 0.0f;
        float       MaxPresentMs  = 0.0f;
        uint32_t    SpikeCount    = 0;
        std::string Reason;
    };

    struct VertexTransformUbo
    {
        float Scale[2]     = {1.0f, 1.0f};
        float Translate[2] = {0.0f, 0.0f};
    };

    double NowSeconds()
    {
        using Clock = std::chrono::steady_clock;
        static const auto s_Start = Clock::now();
        return std::chrono::duration<double>(Clock::now() - s_Start).count();
    }

    [[noreturn]] void ThrowVk(VkResult result, const char* what)
    {
        std::ostringstream oss;
        oss << what << " (VkResult=" << static_cast<int>(result) << ")";
        throw std::runtime_error(oss.str());
    }

    void CheckVk(VkResult result, const char* what)
    {
        if (result != VK_SUCCESS)
            ThrowVk(result, what);
    }

    void CheckCuda(cudaError_t err, const char* what)
    {
        if (err != cudaSuccess)
        {
            std::ostringstream oss;
            oss << what << " failed: " << cudaGetErrorString(err);
            throw std::runtime_error(oss.str());
        }
    }

    std::vector<uint32_t> ToSpvWords(const uint8_t* data, size_t sizeBytes)
    {
        if (sizeBytes % sizeof(uint32_t) != 0)
            throw std::runtime_error("SPIR-V blob size is not 4-byte aligned");
        std::vector<uint32_t> words(sizeBytes / sizeof(uint32_t));
        std::memcpy(words.data(), data, sizeBytes);
        return words;
    }

    std::string EscapeCsv(std::string_view in)
    {
        std::string out;
        out.reserve(in.size());
        for (char c : in)
            out.push_back(c == '"' ? '\'' : c);
        return out;
    }

    class CsvLogger
    {
    public:
        CsvLogger()
        {
            std::filesystem::create_directories("logs");
            const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif

            std::ostringstream name;
            name << "logs/vk_cuda_interop_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".csv";
            m_Stream.open(name.str(), std::ios::out | std::ios::trunc);
            if (!m_Stream.is_open())
                throw std::runtime_error("Failed to open CSV output file");

            m_Stream << "TimeSec,Phase,FrameIndex,Particles,PCISPHIterations,"
                        "CudaWaitMs,CudaKernelMs,CudaSignalMs,"
                        "VkAcquireMs,VkSubmitMs,VkPresentMs,"
                        "CalibrationP95Ms,CalibrationMaxMs,CalibrationSpikeCount,"
                        "WaitVulkanValue,CudaSignalValue,VkSignalValue,Status,Error\n";
            m_Stream.flush();
            std::cout << "[InteropDemo] CSV: " << name.str() << "\n";
        }

        void WriteFrame(std::string_view phase,
                        uint64_t         frameIndex,
                        uint32_t         particles,
                        uint32_t         pcisphIterations,
                        const FrameTelemetry& telemetry,
                        std::string_view status,
                        std::string_view error)
        {
            m_Stream << std::fixed << std::setprecision(6) << NowSeconds() << ',' << phase << ',' << frameIndex << ','
                     << particles << ',' << pcisphIterations << ',' << telemetry.CudaWaitMs << ','
                     << telemetry.CudaKernelMs << ',' << telemetry.CudaSignalMs << ',' << telemetry.VkAcquireMs << ','
                     << telemetry.VkSubmitMs << ',' << telemetry.VkPresentMs << ','
                     << telemetry.CalibrationP95Ms << ',' << telemetry.CalibrationMaxMs << ','
                     << telemetry.CalibrationSpikeCount << ',' << telemetry.WaitVulkanValue << ','
                     << telemetry.CudaSignalValue << ',' << telemetry.VkSignalValue << ',' << status << ','
                     << '"' << EscapeCsv(error) << '"' << '\n';
        }

        void WriteCalibrationSummary(const CalibrationSummary& summary)
        {
            FrameTelemetry t{};
            t.CalibrationP95Ms      = summary.P95PresentMs;
            t.CalibrationMaxMs      = summary.MaxPresentMs;
            t.CalibrationSpikeCount = summary.SpikeCount;
            WriteFrame("calibration_summary", 0, summary.ParticleCount, 0, t,
                       summary.Passed ? "pass" : "fail", summary.Reason);
        }

        void Flush() { m_Stream.flush(); }

    private:
        std::ofstream m_Stream;
    };

    DemoConfig ParseArgs(int argc, char** argv)
    {
        DemoConfig cfg;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg(argv[i]);
            auto ParseUnsigned = [](const std::string& input, uint32_t& out)
            {
                try
                {
                    const auto value = std::stoul(input);
                    if (value > std::numeric_limits<uint32_t>::max())
                        return false;
                    out = static_cast<uint32_t>(value);
                    return true;
                }
                catch (...)
                {
                    return false;
                }
            };

            if (arg.rfind("--particles=", 0) == 0)
            {
                ParseUnsigned(arg.substr(std::string("--particles=").size()), cfg.Particles);
            }
            else if (arg.rfind("--pcisph-iters=", 0) == 0)
            {
                ParseUnsigned(arg.substr(std::string("--pcisph-iters=").size()), cfg.PCISPHIterations);
                cfg.PCISPHIterations = std::max(1u, cfg.PCISPHIterations);
            }
            else if (arg.rfind("--duration-sec=", 0) == 0)
            {
                ParseUnsigned(arg.substr(std::string("--duration-sec=").size()), cfg.DurationSec);
                cfg.DurationSec = std::max(5u, cfg.DurationSec);
            }
            else if (arg.rfind("--calibrate=", 0) == 0)
            {
                const std::string v = arg.substr(std::string("--calibrate=").size());
                cfg.Calibrate        = (v == "1" || v == "on" || v == "true");
            }
            else if (arg == "--no-calibrate")
            {
                cfg.Calibrate = false;
            }
            else if (arg.rfind("--vsync=", 0) == 0)
            {
                const std::string v = arg.substr(std::string("--vsync=").size());
                cfg.VSync            = (v == "1" || v == "on" || v == "true");
            }
            else if (arg == "--help")
            {
                std::cout << "VulkanCudaInteropDemo options:\n"
                             "  --particles=<N>\n"
                             "  --pcisph-iters=<N>\n"
                             "  --duration-sec=<N>\n"
                             "  --calibrate=<0|1> | --no-calibrate\n"
                             "  --vsync=<0|1>\n";
            }
        }
        return cfg;
    }

    struct VulkanExternalExport
    {
        static VkExternalMemoryHandleTypeFlagBits VkMemoryHandleType()
        {
            switch (GetDefaultExternalHandleType())
            {
            case ExternalHandleType::OpaqueWin32:
                return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            case ExternalHandleType::OpaqueFd:
                return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
            default:
                throw std::runtime_error("Unsupported external memory handle type");
            }
        }

        static VkExternalSemaphoreHandleTypeFlagBits VkSemaphoreHandleType()
        {
            switch (GetDefaultExternalHandleType())
            {
            case ExternalHandleType::OpaqueWin32:
                return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            case ExternalHandleType::OpaqueFd:
                return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
            default:
                throw std::runtime_error("Unsupported external semaphore handle type");
            }
        }

        static cudaExternalMemoryHandleType CudaMemoryHandleType()
        {
            switch (GetDefaultExternalHandleType())
            {
            case ExternalHandleType::OpaqueWin32:
                return cudaExternalMemoryHandleTypeOpaqueWin32;
            case ExternalHandleType::OpaqueFd:
                return cudaExternalMemoryHandleTypeOpaqueFd;
            default:
                throw std::runtime_error("Unsupported CUDA external memory handle type");
            }
        }

        static cudaExternalSemaphoreHandleType CudaSemaphoreHandleType()
        {
            switch (GetDefaultExternalHandleType())
            {
            case ExternalHandleType::OpaqueWin32:
                return cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
            case ExternalHandleType::OpaqueFd:
                return cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
            default:
                throw std::runtime_error("Unsupported CUDA external semaphore handle type");
            }
        }

        static OwnedInteropHandle ExportMemory(VkDevice device, VkDeviceMemory memory)
        {
            OwnedInteropHandle out{};
#ifdef _WIN32
            const auto pfn = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
                vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR"));
            if (!pfn)
                throw std::runtime_error("vkGetMemoryWin32HandleKHR is unavailable");

            VkMemoryGetWin32HandleInfoKHR info{};
            info.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
            info.memory     = memory;
            info.handleType = VkMemoryHandleType();

            HANDLE handle = nullptr;
            CheckVk(pfn(device, &info, &handle), "vkGetMemoryWin32HandleKHR");
            out.Value = handle;
#else
            const auto pfn = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
            if (!pfn)
                throw std::runtime_error("vkGetMemoryFdKHR is unavailable");

            VkMemoryGetFdInfoKHR info{};
            info.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
            info.memory     = memory;
            info.handleType = VkMemoryHandleType();

            int fd = -1;
            CheckVk(pfn(device, &info, &fd), "vkGetMemoryFdKHR");
            out.Value = fd;
#endif
            return out;
        }

        static OwnedInteropHandle ExportTimelineSemaphore(VkDevice device, VkSemaphore semaphore)
        {
            OwnedInteropHandle out{};
#ifdef _WIN32
            const auto pfn = reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
                vkGetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR"));
            if (!pfn)
                throw std::runtime_error("vkGetSemaphoreWin32HandleKHR is unavailable");

            VkSemaphoreGetWin32HandleInfoKHR info{};
            info.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
            info.semaphore  = semaphore;
            info.handleType = VkSemaphoreHandleType();

            HANDLE handle = nullptr;
            CheckVk(pfn(device, &info, &handle), "vkGetSemaphoreWin32HandleKHR");
            out.Value = handle;
#else
            const auto pfn = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
                vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
            if (!pfn)
                throw std::runtime_error("vkGetSemaphoreFdKHR is unavailable");

            VkSemaphoreGetFdInfoKHR info{};
            info.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
            info.semaphore  = semaphore;
            info.handleType = VkSemaphoreHandleType();

            int fd = -1;
            CheckVk(pfn(device, &info, &fd), "vkGetSemaphoreFdKHR");
            out.Value = fd;
#endif
            return out;
        }
    };

    class CudaExternalInteropContext
    {
    public:
        CudaExternalInteropContext() = default;
        ~CudaExternalInteropContext() { Destroy(); }

        void Initialize(OwnedInteropHandle memoryHandle,
                        OwnedInteropHandle semaphoreHandle,
                        size_t             exportMemorySize)
        {
            if (!memoryHandle.IsValid() || !semaphoreHandle.IsValid())
                throw std::runtime_error("Invalid exported handle for CUDA import");

            CheckCuda(cudaSetDevice(0), "cudaSetDevice(0)");
            CheckCuda(cudaStreamCreate(&m_Stream), "cudaStreamCreate");

            cudaExternalMemoryHandleDesc memDesc{};
            memDesc.type = VulkanExternalExport::CudaMemoryHandleType();
#ifdef _WIN32
            memDesc.handle.win32.handle = static_cast<HANDLE>(memoryHandle.Value);
#else
            memDesc.handle.fd = memoryHandle.Value;
#endif
            memDesc.size = exportMemorySize;
            CheckCuda(cudaImportExternalMemory(&m_ExternalMemory, &memDesc), "cudaImportExternalMemory");
            CloseNativeHandle(memoryHandle, GetDefaultExternalHandleType());

            cudaExternalMemoryBufferDesc bufferDesc{};
            bufferDesc.offset = 0;
            bufferDesc.size   = exportMemorySize;
            CheckCuda(cudaExternalMemoryGetMappedBuffer(reinterpret_cast<void**>(&m_DeviceVertices), m_ExternalMemory,
                                                        &bufferDesc),
                      "cudaExternalMemoryGetMappedBuffer");

            cudaExternalSemaphoreHandleDesc semDesc{};
            semDesc.type = VulkanExternalExport::CudaSemaphoreHandleType();
#ifdef _WIN32
            semDesc.handle.win32.handle = static_cast<HANDLE>(semaphoreHandle.Value);
#else
            semDesc.handle.fd = semaphoreHandle.Value;
#endif
            CheckCuda(cudaImportExternalSemaphore(&m_ExternalSemaphore, &semDesc), "cudaImportExternalSemaphore");
            CloseNativeHandle(semaphoreHandle, GetDefaultExternalHandleType());

            CheckCuda(cudaEventCreate(&m_WaitStart), "cudaEventCreate(waitStart)");
            CheckCuda(cudaEventCreate(&m_WaitEnd), "cudaEventCreate(waitEnd)");
            CheckCuda(cudaEventCreate(&m_KernelStart), "cudaEventCreate(kernelStart)");
            CheckCuda(cudaEventCreate(&m_KernelEnd), "cudaEventCreate(kernelEnd)");
            CheckCuda(cudaEventCreate(&m_SignalStart), "cudaEventCreate(signalStart)");
            CheckCuda(cudaEventCreate(&m_SignalEnd), "cudaEventCreate(signalEnd)");
        }

        void Destroy()
        {
            if (m_DeviceVertices)
            {
                cudaFree(m_DeviceVertices);
                m_DeviceVertices = nullptr;
            }

            if (m_ExternalSemaphore)
            {
                cudaDestroyExternalSemaphore(m_ExternalSemaphore);
                m_ExternalSemaphore = nullptr;
            }

            if (m_ExternalMemory)
            {
                cudaDestroyExternalMemory(m_ExternalMemory);
                m_ExternalMemory = nullptr;
            }

            DestroyEvent(m_WaitStart);
            DestroyEvent(m_WaitEnd);
            DestroyEvent(m_KernelStart);
            DestroyEvent(m_KernelEnd);
            DestroyEvent(m_SignalStart);
            DestroyEvent(m_SignalEnd);

            if (m_Stream)
            {
                cudaStreamDestroy(m_Stream);
                m_Stream = nullptr;
            }
        }

        FrameTelemetry RunFrame(const InteropFrameSyncValues& syncValues,
                                uint32_t                      particleCount,
                                uint32_t                      pcisphIterations,
                                float                         timeSeconds)
        {
            FrameTelemetry telemetry{};
            telemetry.WaitVulkanValue = syncValues.WaitVulkanValue;
            telemetry.CudaSignalValue = syncValues.CudaSignalValue;
            telemetry.VkSignalValue   = syncValues.VulkanSignalValue;

            cudaExternalSemaphoreWaitParams waitParams{};
            waitParams.params.fence.value = syncValues.WaitVulkanValue;

            cudaExternalSemaphoreSignalParams signalParams{};
            signalParams.params.fence.value = syncValues.CudaSignalValue;

            CheckCuda(cudaEventRecord(m_WaitStart, m_Stream), "cudaEventRecord(waitStart)");
            CheckCuda(cudaWaitExternalSemaphoresAsync(&m_ExternalSemaphore, &waitParams, 1, m_Stream),
                      "cudaWaitExternalSemaphoresAsync");
            CheckCuda(cudaEventRecord(m_WaitEnd, m_Stream), "cudaEventRecord(waitEnd)");

            CheckCuda(cudaEventRecord(m_KernelStart, m_Stream), "cudaEventRecord(kernelStart)");
            InteropDemo::LaunchUpdateParticlesKernel(m_DeviceVertices, particleCount, timeSeconds, pcisphIterations,
                                                     m_Stream);
            CheckCuda(cudaGetLastError(), "LaunchUpdateParticlesKernel");
            CheckCuda(cudaEventRecord(m_KernelEnd, m_Stream), "cudaEventRecord(kernelEnd)");

            CheckCuda(cudaEventRecord(m_SignalStart, m_Stream), "cudaEventRecord(signalStart)");
            CheckCuda(cudaSignalExternalSemaphoresAsync(&m_ExternalSemaphore, &signalParams, 1, m_Stream),
                      "cudaSignalExternalSemaphoresAsync");
            CheckCuda(cudaEventRecord(m_SignalEnd, m_Stream), "cudaEventRecord(signalEnd)");
            CheckCuda(cudaEventSynchronize(m_SignalEnd), "cudaEventSynchronize(signalEnd)");

            CheckCuda(cudaEventElapsedTime(&telemetry.CudaWaitMs, m_WaitStart, m_WaitEnd),
                      "cudaEventElapsedTime(wait)");
            CheckCuda(cudaEventElapsedTime(&telemetry.CudaKernelMs, m_KernelStart, m_KernelEnd),
                      "cudaEventElapsedTime(kernel)");
            CheckCuda(cudaEventElapsedTime(&telemetry.CudaSignalMs, m_SignalStart, m_SignalEnd),
                      "cudaEventElapsedTime(signal)");

            return telemetry;
        }

    private:
        void DestroyEvent(cudaEvent_t& evt)
        {
            if (evt)
            {
                cudaEventDestroy(evt);
                evt = nullptr;
            }
        }

        void CloseNativeHandle(OwnedInteropHandle& handle, ExternalHandleType handleType)
        {
            if (!handle.IsValid())
                return;

            if (!Engine::CudaInterop::ShouldCloseHandleAfterCudaImport(handleType))
                return;

#ifdef _WIN32
            CloseHandle(static_cast<HANDLE>(handle.Value));
#else
            ::close(handle.Value);
#endif
            handle.MarkImportedByCuda();
        }

    private:
        cudaExternalMemory_t    m_ExternalMemory    = nullptr;
        cudaExternalSemaphore_t m_ExternalSemaphore = nullptr;
        InteropDemo::CudaParticleVertex* m_DeviceVertices = nullptr;
        cudaStream_t            m_Stream            = nullptr;

        cudaEvent_t m_WaitStart   = nullptr;
        cudaEvent_t m_WaitEnd     = nullptr;
        cudaEvent_t m_KernelStart = nullptr;
        cudaEvent_t m_KernelEnd   = nullptr;
        cudaEvent_t m_SignalStart = nullptr;
        cudaEvent_t m_SignalEnd   = nullptr;
    };

    struct QueueFamilySelection
    {
        uint32_t GraphicsFamily = UINT32_MAX;
        uint32_t PresentFamily  = UINT32_MAX;

        bool IsComplete() const
        {
            return GraphicsFamily != UINT32_MAX && PresentFamily != UINT32_MAX;
        }
    };

    struct SwapchainSupportInfo
    {
        VkSurfaceCapabilitiesKHR        Capabilities{};
        std::vector<VkSurfaceFormatKHR> Formats;
        std::vector<VkPresentModeKHR>   PresentModes;
    };

    class VulkanDemoApp
    {
    public:
        explicit VulkanDemoApp(const DemoConfig& config)
            : m_Config(config), m_MaxParticles(std::max(config.Particles, 200000u))
        {
        }

        ~VulkanDemoApp()
        {
            Cleanup();
        }

        void Run(CsvLogger& logger)
        {
            Initialize();

            uint32_t activeParticles = m_Config.Particles;
            if (m_Config.Calibrate)
                activeParticles = RunCalibration(logger);

            std::cout << "[InteropDemo] Run start: particles=" << activeParticles
                      << ", pcisphIters=" << m_Config.PCISPHIterations
                      << ", durationSec=" << m_Config.DurationSec << "\n";

            const double endTime = NowSeconds() + static_cast<double>(m_Config.DurationSec);
            while (!glfwWindowShouldClose(m_Window) && NowSeconds() < endTime)
            {
                glfwPollEvents();
                FrameTelemetry telemetry{};
                std::string    status = "ok";
                std::string    error;
                try
                {
                    telemetry = RenderFrame(activeParticles, m_Config.PCISPHIterations, static_cast<float>(NowSeconds()));
                }
                catch (const std::exception& ex)
                {
                    telemetry.Success = false;
                    telemetry.Error   = ex.what();
                    status            = "error";
                    error             = ex.what();
                    logger.WriteFrame("run", m_FrameIndex, activeParticles, m_Config.PCISPHIterations, telemetry,
                                      status, error);
                    throw;
                }

                logger.WriteFrame("run", m_FrameIndex, activeParticles, m_Config.PCISPHIterations, telemetry, status,
                                  error);
                ++m_FrameIndex;
            }

            logger.Flush();
        }

    private:
        uint32_t RunCalibration(CsvLogger& logger)
        {
            static const std::array<uint32_t, 5> kProfiles = {50000u, 75000u, 100000u, 150000u, 200000u};
            static constexpr uint32_t            kFramesPerProfile = 150u;

            uint32_t selected = kProfiles.front();
            for (uint32_t profile : kProfiles)
            {
                if (profile > m_MaxParticles)
                    break;

                std::vector<float> presentSamples;
                presentSamples.reserve(kFramesPerProfile);

                uint32_t spikes = 0;
                bool     failed = false;
                std::string failReason;

                std::cout << "[InteropDemo][Calib] profile=" << profile << " ...\n";

                for (uint32_t i = 0; i < kFramesPerProfile; ++i)
                {
                    if (glfwWindowShouldClose(m_Window))
                    {
                        failed     = true;
                        failReason = "window closed by user";
                        break;
                    }

                    glfwPollEvents();

                    FrameTelemetry telemetry{};
                    try
                    {
                        telemetry =
                            RenderFrame(profile, m_Config.PCISPHIterations, static_cast<float>(NowSeconds()));
                    }
                    catch (const std::exception& ex)
                    {
                        failed     = true;
                        failReason = ex.what();
                        telemetry.Success = false;
                        telemetry.Error   = failReason;
                    }

                    logger.WriteFrame("calibration", m_FrameIndex, profile, m_Config.PCISPHIterations, telemetry,
                                      telemetry.Success ? "ok" : "error", telemetry.Error);
                    ++m_FrameIndex;

                    if (!telemetry.Success)
                    {
                        failed = true;
                        if (failReason.empty())
                            failReason = telemetry.Error;
                        break;
                    }

                    presentSamples.push_back(telemetry.VkPresentMs);
                    if (telemetry.VkPresentMs > 50.0f)
                        ++spikes;
                }

                CalibrationSummary summary{};
                summary.ParticleCount = profile;
                summary.SpikeCount    = spikes;

                if (!failed && !presentSamples.empty())
                {
                    std::sort(presentSamples.begin(), presentSamples.end());
                    const size_t p95Index = static_cast<size_t>(std::floor((presentSamples.size() - 1) * 0.95));
                    summary.P95PresentMs = presentSamples[p95Index];
                    summary.MaxPresentMs = presentSamples.back();
                    summary.Passed = (summary.P95PresentMs < 20.0f) && (summary.MaxPresentMs < 120.0f) &&
                                     (spikes <= presentSamples.size() / 25u);
                    if (!summary.Passed)
                    {
                        std::ostringstream oss;
                        oss << "stability threshold failed (p95=" << summary.P95PresentMs
                            << "ms, max=" << summary.MaxPresentMs << "ms, spikes=" << spikes << ')';
                        summary.Reason = oss.str();
                    }
                }
                else
                {
                    summary.Passed = false;
                    summary.Reason = failReason.empty() ? "frame execution failed" : failReason;
                }

                logger.WriteCalibrationSummary(summary);

                if (summary.Passed)
                {
                    selected = profile;
                    std::cout << "[InteropDemo][Calib] PASS profile=" << profile
                              << " p95=" << summary.P95PresentMs
                              << " max=" << summary.MaxPresentMs << '\n';
                }
                else
                {
                    std::cout << "[InteropDemo][Calib] FAIL profile=" << profile
                              << " reason=" << summary.Reason << '\n';
                    break;
                }
            }

            std::cout << "[InteropDemo][Calib] Selected particles=" << selected << "\n";
            return selected;
        }

        void Initialize()
        {
            InitializeWindow();
            CreateInstance();
            CreateSurface();
            PickPhysicalDevice();
            CreateLogicalDevice();
            CreateSwapchain();
            CreateImageViews();
            CreateRenderPass();
            CreateCommandPoolAndBuffers();
            CreateDescriptorResources();
            CreatePipeline();
            CreateFramebuffers();
            CreateSyncObjects();
            CreateSharedVertexBuffer();
            InitializeCudaInterop();
        }

        void Cleanup()
        {
            if (m_Device != VK_NULL_HANDLE)
                vkDeviceWaitIdle(m_Device);

            m_CudaInterop.Destroy();

            if (m_SharedVertexBuffer != VK_NULL_HANDLE)
                vkDestroyBuffer(m_Device, m_SharedVertexBuffer, nullptr);
            if (m_SharedVertexMemory != VK_NULL_HANDLE)
                vkFreeMemory(m_Device, m_SharedVertexMemory, nullptr);

            if (m_InFlightFence != VK_NULL_HANDLE)
                vkDestroyFence(m_Device, m_InFlightFence, nullptr);
            if (m_ImageAvailable != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Device, m_ImageAvailable, nullptr);
            if (m_RenderFinished != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Device, m_RenderFinished, nullptr);
            if (m_TimelineSemaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Device, m_TimelineSemaphore, nullptr);

            if (m_CommandPool != VK_NULL_HANDLE)
                vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);

            for (auto framebuffer : m_SwapchainFramebuffers)
                vkDestroyFramebuffer(m_Device, framebuffer, nullptr);

            if (m_GraphicsPipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
            if (m_PipelineLayout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
            if (m_RenderPass != VK_NULL_HANDLE)
                vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);

            if (m_WhiteSampler != VK_NULL_HANDLE)
                vkDestroySampler(m_Device, m_WhiteSampler, nullptr);
            if (m_WhiteImageView != VK_NULL_HANDLE)
                vkDestroyImageView(m_Device, m_WhiteImageView, nullptr);
            if (m_WhiteImage != VK_NULL_HANDLE)
                vkDestroyImage(m_Device, m_WhiteImage, nullptr);
            if (m_WhiteImageMemory != VK_NULL_HANDLE)
                vkFreeMemory(m_Device, m_WhiteImageMemory, nullptr);

            if (m_TransformBuffer != VK_NULL_HANDLE)
                vkDestroyBuffer(m_Device, m_TransformBuffer, nullptr);
            if (m_TransformMemory != VK_NULL_HANDLE)
                vkFreeMemory(m_Device, m_TransformMemory, nullptr);

            if (m_DescriptorPool != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
            if (m_Set2TextureLayout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(m_Device, m_Set2TextureLayout, nullptr);
            if (m_Set1UboLayout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(m_Device, m_Set1UboLayout, nullptr);
            if (m_Set0Layout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(m_Device, m_Set0Layout, nullptr);

            for (auto view : m_SwapchainImageViews)
                vkDestroyImageView(m_Device, view, nullptr);

            if (m_Swapchain != VK_NULL_HANDLE)
                vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);

            if (m_Device != VK_NULL_HANDLE)
                vkDestroyDevice(m_Device, nullptr);

            if (m_Surface != VK_NULL_HANDLE)
                vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

            if (m_Instance != VK_NULL_HANDLE)
                vkDestroyInstance(m_Instance, nullptr);

            if (m_Window)
            {
                glfwDestroyWindow(m_Window);
                m_Window = nullptr;
            }
            glfwTerminate();
        }

        FrameTelemetry RenderFrame(uint32_t particles, uint32_t pcisphIterations, float timeSeconds)
        {
            FrameTelemetry telemetry{};

            CheckVk(vkWaitForFences(m_Device, 1, &m_InFlightFence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
            CheckVk(vkResetFences(m_Device, 1, &m_InFlightFence), "vkResetFences");

            const auto acquireBegin = std::chrono::high_resolution_clock::now();
            uint32_t   imageIndex   = 0;
            VkResult acquireResult = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, m_ImageAvailable,
                                                            VK_NULL_HANDLE, &imageIndex);
            const auto acquireEnd = std::chrono::high_resolution_clock::now();
            if (acquireResult != VK_SUCCESS)
                ThrowVk(acquireResult, "vkAcquireNextImageKHR");
            telemetry.VkAcquireMs = std::chrono::duration<float, std::milli>(acquireEnd - acquireBegin).count();

            const InteropFrameSyncValues syncValues = BuildInteropFrameSyncValues(m_FrameIndex);
            telemetry = m_CudaInterop.RunFrame(syncValues, particles, pcisphIterations, timeSeconds);

            RecordCommandBuffer(imageIndex, particles);

            VkSemaphore waitSemaphores[2] = {m_ImageAvailable, m_TimelineSemaphore};
            VkPipelineStageFlags waitStages[2] = {
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            };
            VkSemaphore signalSemaphores[2] = {m_RenderFinished, m_TimelineSemaphore};
            uint64_t    waitValues[2]       = {0ull, syncValues.CudaSignalValue};
            uint64_t    signalValues[2]     = {0ull, syncValues.VulkanSignalValue};

            VkTimelineSemaphoreSubmitInfo timelineInfo{};
            timelineInfo.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
            timelineInfo.waitSemaphoreValueCount   = 2;
            timelineInfo.pWaitSemaphoreValues      = waitValues;
            timelineInfo.signalSemaphoreValueCount = 2;
            timelineInfo.pSignalSemaphoreValues    = signalValues;

            VkSubmitInfo submitInfo{};
            submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.pNext                = &timelineInfo;
            submitInfo.waitSemaphoreCount   = 2;
            submitInfo.pWaitSemaphores      = waitSemaphores;
            submitInfo.pWaitDstStageMask    = waitStages;
            submitInfo.commandBufferCount   = 1;
            submitInfo.pCommandBuffers      = &m_CommandBuffers[imageIndex];
            submitInfo.signalSemaphoreCount = 2;
            submitInfo.pSignalSemaphores    = signalSemaphores;

            const auto submitBegin = std::chrono::high_resolution_clock::now();
            CheckVk(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFence), "vkQueueSubmit");
            const auto submitEnd = std::chrono::high_resolution_clock::now();
            telemetry.VkSubmitMs = std::chrono::duration<float, std::milli>(submitEnd - submitBegin).count();

            VkPresentInfoKHR presentInfo{};
            presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores    = &m_RenderFinished;
            presentInfo.swapchainCount     = 1;
            presentInfo.pSwapchains        = &m_Swapchain;
            presentInfo.pImageIndices      = &imageIndex;

            const auto presentBegin = std::chrono::high_resolution_clock::now();
            VkResult presentResult  = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
            const auto presentEnd   = std::chrono::high_resolution_clock::now();
            if (presentResult != VK_SUCCESS)
                ThrowVk(presentResult, "vkQueuePresentKHR");
            telemetry.VkPresentMs = std::chrono::duration<float, std::milli>(presentEnd - presentBegin).count();
            telemetry.WaitVulkanValue = syncValues.WaitVulkanValue;
            telemetry.CudaSignalValue = syncValues.CudaSignalValue;
            telemetry.VkSignalValue   = syncValues.VulkanSignalValue;

            return telemetry;
        }

        void InitializeWindow()
        {
            if (!glfwInit())
                throw std::runtime_error("glfwInit failed");

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            m_Window = glfwCreateWindow(1280, 720, "Vulkan-CUDA External Interop Demo", nullptr, nullptr);
            if (!m_Window)
                throw std::runtime_error("glfwCreateWindow failed");
        }

        void CreateInstance()
        {
            uint32_t glfwExtCount = 0;
            const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
            if (!glfwExts || glfwExtCount == 0)
                throw std::runtime_error("glfwGetRequiredInstanceExtensions returned none");

            VkApplicationInfo appInfo{};
            appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName   = "VulkanCudaInteropDemo";
            appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.pEngineName        = "GraduationProject";
            appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
            appInfo.apiVersion         = VK_API_VERSION_1_2;

            VkInstanceCreateInfo createInfo{};
            createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo        = &appInfo;
            createInfo.enabledExtensionCount   = glfwExtCount;
            createInfo.ppEnabledExtensionNames = glfwExts;

            CheckVk(vkCreateInstance(&createInfo, nullptr, &m_Instance), "vkCreateInstance");
        }

        void CreateSurface()
        {
            CheckVk(glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface), "glfwCreateWindowSurface");
        }

        QueueFamilySelection FindQueueFamilies(VkPhysicalDevice device) const
        {
            QueueFamilySelection out{};

            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, families.data());

            for (uint32_t i = 0; i < queueFamilyCount; ++i)
            {
                if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    out.GraphicsFamily = i;

                VkBool32 presentSupport = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
                if (presentSupport)
                    out.PresentFamily = i;
            }
            return out;
        }

        bool CheckDeviceExtensionSupport(VkPhysicalDevice device, const std::vector<const char*>& required) const
        {
            uint32_t count = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
            std::vector<VkExtensionProperties> available(count);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

            for (const char* req : required)
            {
                bool found = false;
                for (const auto& ext : available)
                {
                    if (std::string_view(ext.extensionName) == req)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
            }
            return true;
        }

        SwapchainSupportInfo QuerySwapchainSupport(VkPhysicalDevice device) const
        {
            SwapchainSupportInfo info{};
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &info.Capabilities);

            uint32_t formatCount = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);
            info.Formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, info.Formats.data());

            uint32_t modeCount = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &modeCount, nullptr);
            info.PresentModes.resize(modeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &modeCount, info.PresentModes.data());

            return info;
        }

        bool SupportsTimelineSemaphores(VkPhysicalDevice device) const
        {
            VkPhysicalDeviceTimelineSemaphoreFeatures timeline{};
            timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;

            VkPhysicalDeviceFeatures2 features2{};
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2.pNext = &timeline;
            vkGetPhysicalDeviceFeatures2(device, &features2);
            return timeline.timelineSemaphore == VK_TRUE;
        }

        bool SupportsExternalVertexBufferInterop(VkPhysicalDevice device) const
        {
            VkPhysicalDeviceExternalBufferInfo info{};
            info.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
            info.usage      = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            info.handleType = VulkanExternalExport::VkMemoryHandleType();

            VkExternalBufferProperties props{};
            props.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
            vkGetPhysicalDeviceExternalBufferProperties(device, &info, &props);

            const VkExternalMemoryFeatureFlags features =
                props.externalMemoryProperties.externalMemoryFeatures;
            const bool importable = (features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
            const bool exportable = (features & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0;
            return importable && exportable;
        }

        bool SupportsExternalTimelineSemaphoreInterop(VkPhysicalDevice device) const
        {
            VkPhysicalDeviceExternalSemaphoreInfo info{};
            info.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
            info.handleType = VulkanExternalExport::VkSemaphoreHandleType();

            VkExternalSemaphoreProperties props{};
            props.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
            vkGetPhysicalDeviceExternalSemaphoreProperties(device, &info, &props);

            const VkExternalSemaphoreFeatureFlags features = props.externalSemaphoreFeatures;
            const bool importable = (features & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) != 0;
            const bool exportable = (features & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) != 0;
            return importable && exportable;
        }

        void PickPhysicalDevice()
        {
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
            if (deviceCount == 0)
                throw std::runtime_error("No Vulkan physical device found");

            std::vector<VkPhysicalDevice> devices(deviceCount);
            vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

            std::vector<const char*> requiredExtensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
#ifdef _WIN32
                VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
#else
                VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
#endif
            };

            std::vector<std::string> rejectReasons;
            rejectReasons.reserve(devices.size());

            for (auto device : devices)
            {
                VkPhysicalDeviceProperties props{};
                vkGetPhysicalDeviceProperties(device, &props);
                const std::string deviceName = props.deviceName;

                const auto queues = FindQueueFamilies(device);
                if (!queues.IsComplete())
                {
                    rejectReasons.push_back(deviceName + ": missing graphics/present queue family");
                    continue;
                }

                if (!CheckDeviceExtensionSupport(device, requiredExtensions))
                {
                    rejectReasons.push_back(deviceName + ": missing required Vulkan extensions");
                    continue;
                }

                if (!SupportsTimelineSemaphores(device))
                {
                    rejectReasons.push_back(deviceName + ": timeline semaphore unsupported");
                    continue;
                }

                if (!SupportsExternalVertexBufferInterop(device))
                {
                    rejectReasons.push_back(deviceName + ": external vertex buffer interop unsupported");
                    continue;
                }

                if (!SupportsExternalTimelineSemaphoreInterop(device))
                {
                    rejectReasons.push_back(deviceName + ": external timeline semaphore interop unsupported");
                    continue;
                }

                const auto swapchain = QuerySwapchainSupport(device);
                if (swapchain.Formats.empty() || swapchain.PresentModes.empty())
                {
                    rejectReasons.push_back(deviceName + ": swapchain formats/present modes unavailable");
                    continue;
                }

                m_PhysicalDevice     = device;
                m_QueueFamilyIndices = queues;
                break;
            }

            if (m_PhysicalDevice == VK_NULL_HANDLE)
            {
                std::ostringstream oss;
                oss << "No suitable Vulkan device with external memory/timeline support";
                if (!rejectReasons.empty())
                {
                    oss << ". Reasons:";
                    for (const auto& reason : rejectReasons)
                        oss << " [" << reason << ']';
                }
                throw std::runtime_error(oss.str());
            }
        }

        void CreateLogicalDevice()
        {
            std::vector<const char*> extensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
#ifdef _WIN32
                VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
#else
                VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
#endif
            };

            std::vector<uint32_t> uniqueFamilies = {m_QueueFamilyIndices.GraphicsFamily};
            if (m_QueueFamilyIndices.PresentFamily != m_QueueFamilyIndices.GraphicsFamily)
                uniqueFamilies.push_back(m_QueueFamilyIndices.PresentFamily);

            float priority = 1.0f;
            std::vector<VkDeviceQueueCreateInfo> queueInfos;
            queueInfos.reserve(uniqueFamilies.size());
            for (uint32_t family : uniqueFamilies)
            {
                VkDeviceQueueCreateInfo q{};
                q.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                q.queueFamilyIndex = family;
                q.queueCount       = 1;
                q.pQueuePriorities = &priority;
                queueInfos.push_back(q);
            }

            VkPhysicalDeviceTimelineSemaphoreFeatures timeline{};
            timeline.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
            timeline.timelineSemaphore = VK_TRUE;

            VkDeviceCreateInfo createInfo{};
            createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pNext                   = &timeline;
            createInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueInfos.size());
            createInfo.pQueueCreateInfos       = queueInfos.data();
            createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();

            CheckVk(vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device), "vkCreateDevice");
            vkGetDeviceQueue(m_Device, m_QueueFamilyIndices.GraphicsFamily, 0, &m_GraphicsQueue);
            vkGetDeviceQueue(m_Device, m_QueueFamilyIndices.PresentFamily, 0, &m_PresentQueue);
        }

        VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
        {
            for (const auto& fmt : formats)
            {
                if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                    return fmt;
            }
            return formats.front();
        }

        VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) const
        {
            if (m_Config.VSync)
                return VK_PRESENT_MODE_FIFO_KHR;

            for (auto mode : modes)
            {
                if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
                    return mode;
            }
            for (auto mode : modes)
            {
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
                    return mode;
            }
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const
        {
            if (caps.currentExtent.width != UINT32_MAX)
                return caps.currentExtent;

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(m_Window, &width, &height);

            VkExtent2D extent{};
            extent.width = static_cast<uint32_t>(std::clamp(width, static_cast<int>(caps.minImageExtent.width),
                                                            static_cast<int>(caps.maxImageExtent.width)));
            extent.height = static_cast<uint32_t>(std::clamp(height, static_cast<int>(caps.minImageExtent.height),
                                                             static_cast<int>(caps.maxImageExtent.height)));
            return extent;
        }

        void CreateSwapchain()
        {
            const auto support      = QuerySwapchainSupport(m_PhysicalDevice);
            const auto surfaceFmt   = ChooseSurfaceFormat(support.Formats);
            const auto presentMode  = ChoosePresentMode(support.PresentModes);
            const auto extent       = ChooseSwapExtent(support.Capabilities);

            uint32_t imageCount = support.Capabilities.minImageCount + 1;
            if (support.Capabilities.maxImageCount > 0 && imageCount > support.Capabilities.maxImageCount)
                imageCount = support.Capabilities.maxImageCount;

            VkSwapchainCreateInfoKHR info{};
            info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            info.surface          = m_Surface;
            info.minImageCount    = imageCount;
            info.imageFormat      = surfaceFmt.format;
            info.imageColorSpace  = surfaceFmt.colorSpace;
            info.imageExtent      = extent;
            info.imageArrayLayers = 1;
            info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

            uint32_t familyIndices[] = {m_QueueFamilyIndices.GraphicsFamily, m_QueueFamilyIndices.PresentFamily};
            if (m_QueueFamilyIndices.GraphicsFamily != m_QueueFamilyIndices.PresentFamily)
            {
                info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
                info.queueFamilyIndexCount = 2;
                info.pQueueFamilyIndices   = familyIndices;
            }
            else
            {
                info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            }

            info.preTransform   = support.Capabilities.currentTransform;
            info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            info.presentMode    = presentMode;
            info.clipped        = VK_TRUE;

            CheckVk(vkCreateSwapchainKHR(m_Device, &info, nullptr, &m_Swapchain), "vkCreateSwapchainKHR");

            vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, nullptr);
            m_SwapchainImages.resize(imageCount);
            vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, m_SwapchainImages.data());

            m_SwapchainFormat = surfaceFmt.format;
            m_SwapchainExtent = extent;
        }

        void CreateImageViews()
        {
            m_SwapchainImageViews.resize(m_SwapchainImages.size());
            for (size_t i = 0; i < m_SwapchainImages.size(); ++i)
            {
                VkImageViewCreateInfo info{};
                info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                info.image                           = m_SwapchainImages[i];
                info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
                info.format                          = m_SwapchainFormat;
                info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                info.subresourceRange.baseMipLevel   = 0;
                info.subresourceRange.levelCount     = 1;
                info.subresourceRange.baseArrayLayer = 0;
                info.subresourceRange.layerCount     = 1;

                CheckVk(vkCreateImageView(m_Device, &info, nullptr, &m_SwapchainImageViews[i]),
                        "vkCreateImageView(swapchain)");
            }
        }

        void CreateRenderPass()
        {
            VkAttachmentDescription color{};
            color.format         = m_SwapchainFormat;
            color.samples        = VK_SAMPLE_COUNT_1_BIT;
            color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorRef{};
            colorRef.attachment = 0;
            colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments    = &colorRef;

            VkSubpassDependency dep{};
            dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
            dep.dstSubpass    = 0;
            dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep.srcAccessMask = 0;
            dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo info{};
            info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            info.attachmentCount = 1;
            info.pAttachments    = &color;
            info.subpassCount    = 1;
            info.pSubpasses      = &subpass;
            info.dependencyCount = 1;
            info.pDependencies   = &dep;

            CheckVk(vkCreateRenderPass(m_Device, &info, nullptr, &m_RenderPass), "vkCreateRenderPass");
        }

        uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required) const
        {
            VkPhysicalDeviceMemoryProperties memProps{};
            vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProps);

            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
            {
                const bool typeOk = (typeBits & (1u << i)) != 0;
                const bool propOk = (memProps.memoryTypes[i].propertyFlags & required) == required;
                if (typeOk && propOk)
                    return i;
            }
            throw std::runtime_error("Failed to find required Vulkan memory type");
        }

        void CreateBuffer(VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer& buffer,
                          VkDeviceMemory& memory)
        {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size        = size;
            bufferInfo.usage       = usage;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            CheckVk(vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(m_Device, buffer, &req);

            VkMemoryAllocateInfo alloc{};
            alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize  = req.size;
            alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, properties);

            CheckVk(vkAllocateMemory(m_Device, &alloc, nullptr, &memory), "vkAllocateMemory(buffer)");
            CheckVk(vkBindBufferMemory(m_Device, buffer, memory, 0), "vkBindBufferMemory");
        }

        VkCommandBuffer BeginSingleTimeCommands()
        {
            VkCommandBufferAllocateInfo alloc{};
            alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc.commandPool        = m_CommandPool;
            alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;

            VkCommandBuffer cmd = VK_NULL_HANDLE;
            CheckVk(vkAllocateCommandBuffers(m_Device, &alloc, &cmd), "vkAllocateCommandBuffers(single-time)");

            VkCommandBufferBeginInfo begin{};
            begin.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            CheckVk(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer(single-time)");
            return cmd;
        }

        void EndSingleTimeCommands(VkCommandBuffer cmd)
        {
            CheckVk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer(single-time)");

            VkSubmitInfo submit{};
            submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers    = &cmd;

            CheckVk(vkQueueSubmit(m_GraphicsQueue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit(single-time)");
            CheckVk(vkQueueWaitIdle(m_GraphicsQueue), "vkQueueWaitIdle(single-time)");
            vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &cmd);
        }

        void TransitionImageLayout(VkImage image,
                                   VkImageLayout oldLayout,
                                   VkImageLayout newLayout)
        {
            VkCommandBuffer cmd = BeginSingleTimeCommands();

            VkImageMemoryBarrier barrier{};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                       = oldLayout;
            barrier.newLayout                       = newLayout;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                           = image;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;

            VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                srcStage              = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dstStage              = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                     newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                srcStage              = VK_PIPELINE_STAGE_TRANSFER_BIT;
                dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else
            {
                throw std::runtime_error("Unsupported image layout transition");
            }

            vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            EndSingleTimeCommands(cmd);
        }

        void CopyBufferToImage(VkBuffer buffer, VkImage image)
        {
            VkCommandBuffer cmd = BeginSingleTimeCommands();

            VkBufferImageCopy region{};
            region.bufferOffset                    = 0;
            region.bufferRowLength                 = 0;
            region.bufferImageHeight               = 0;
            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel       = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount     = 1;
            region.imageOffset                     = {0, 0, 0};
            region.imageExtent                     = {1, 1, 1};

            vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            EndSingleTimeCommands(cmd);
        }

        void CreateDescriptorResources()
        {
            VkDescriptorSetLayoutCreateInfo emptyLayoutInfo{};
            emptyLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            CheckVk(vkCreateDescriptorSetLayout(m_Device, &emptyLayoutInfo, nullptr, &m_Set0Layout),
                    "vkCreateDescriptorSetLayout(set0-empty)");

            VkDescriptorSetLayoutBinding uboBinding{};
            uboBinding.binding            = 0;
            uboBinding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboBinding.descriptorCount    = 1;
            uboBinding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT;

            VkDescriptorSetLayoutCreateInfo uboLayoutInfo{};
            uboLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            uboLayoutInfo.bindingCount = 1;
            uboLayoutInfo.pBindings    = &uboBinding;
            CheckVk(vkCreateDescriptorSetLayout(m_Device, &uboLayoutInfo, nullptr, &m_Set1UboLayout),
                    "vkCreateDescriptorSetLayout(set1-ubo)");

            VkDescriptorSetLayoutBinding textureBinding{};
            textureBinding.binding            = 0;
            textureBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureBinding.descriptorCount    = 1;
            textureBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo textureLayoutInfo{};
            textureLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            textureLayoutInfo.bindingCount = 1;
            textureLayoutInfo.pBindings    = &textureBinding;
            CheckVk(vkCreateDescriptorSetLayout(m_Device, &textureLayoutInfo, nullptr, &m_Set2TextureLayout),
                    "vkCreateDescriptorSetLayout(set2-texture)");

            std::array<VkDescriptorPoolSize, 2> poolSizes{};
            poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            poolSizes[0].descriptorCount = 1;
            poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            poolSizes[1].descriptorCount = 1;

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.maxSets       = 2;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes    = poolSizes.data();
            CheckVk(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool),
                    "vkCreateDescriptorPool");

            CreateWhiteTexture();

            CreateBuffer(sizeof(VertexTransformUbo),
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         m_TransformBuffer,
                         m_TransformMemory);

            VertexTransformUbo transform{};
            void* mapped = nullptr;
            CheckVk(vkMapMemory(m_Device, m_TransformMemory, 0, sizeof(VertexTransformUbo), 0, &mapped),
                    "vkMapMemory(transformUbo)");
            std::memcpy(mapped, &transform, sizeof(transform));
            vkUnmapMemory(m_Device, m_TransformMemory);

            const std::array<VkDescriptorSetLayout, 2> setLayouts = {m_Set1UboLayout, m_Set2TextureLayout};
            std::array<VkDescriptorSet, 2> descriptorSets{};

            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool     = m_DescriptorPool;
            alloc.descriptorSetCount = static_cast<uint32_t>(setLayouts.size());
            alloc.pSetLayouts        = setLayouts.data();
            CheckVk(vkAllocateDescriptorSets(m_Device, &alloc, descriptorSets.data()), "vkAllocateDescriptorSets");

            m_UboDescriptorSet     = descriptorSets[0];
            m_TextureDescriptorSet = descriptorSets[1];

            VkDescriptorBufferInfo uboInfo{};
            uboInfo.buffer = m_TransformBuffer;
            uboInfo.offset = 0;
            uboInfo.range  = sizeof(VertexTransformUbo);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler     = m_WhiteSampler;
            imageInfo.imageView   = m_WhiteImageView;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet          = m_UboDescriptorSet;
            writes[0].dstBinding      = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].pBufferInfo     = &uboInfo;

            writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet          = m_TextureDescriptorSet;
            writes[1].dstBinding      = 0;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo      = &imageInfo;

            vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        void CreateWhiteTexture()
        {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType     = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width  = 1;
            imageInfo.extent.height = 1;
            imageInfo.extent.depth  = 1;
            imageInfo.mipLevels     = 1;
            imageInfo.arrayLayers   = 1;
            imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

            CheckVk(vkCreateImage(m_Device, &imageInfo, nullptr, &m_WhiteImage), "vkCreateImage(white)");

            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(m_Device, m_WhiteImage, &req);

            VkMemoryAllocateInfo alloc{};
            alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize  = req.size;
            alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            CheckVk(vkAllocateMemory(m_Device, &alloc, nullptr, &m_WhiteImageMemory), "vkAllocateMemory(white)");
            CheckVk(vkBindImageMemory(m_Device, m_WhiteImage, m_WhiteImageMemory, 0), "vkBindImageMemory(white)");

            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
            CreateBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingMemory);

            void* mapped = nullptr;
            CheckVk(vkMapMemory(m_Device, stagingMemory, 0, 4, 0, &mapped), "vkMapMemory(staging)");
            static const uint32_t kWhite = 0xFFFFFFFFu;
            std::memcpy(mapped, &kWhite, sizeof(kWhite));
            vkUnmapMemory(m_Device, stagingMemory);

            TransitionImageLayout(m_WhiteImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            CopyBufferToImage(stagingBuffer, m_WhiteImage);
            TransitionImageLayout(m_WhiteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
            vkFreeMemory(m_Device, stagingMemory, nullptr);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image                           = m_WhiteImage;
            viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format                          = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel   = 0;
            viewInfo.subresourceRange.levelCount     = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount     = 1;
            CheckVk(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_WhiteImageView),
                    "vkCreateImageView(white)");

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter    = VK_FILTER_NEAREST;
            samplerInfo.minFilter    = VK_FILTER_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.maxLod       = 0.0f;

            CheckVk(vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_WhiteSampler), "vkCreateSampler");
        }

        void CreatePipeline()
        {
            const std::vector<uint32_t> vertWords = ToSpvWords(spirv_vertex, sizeof(spirv_vertex));
            const std::vector<uint32_t> fragWords = ToSpvWords(spirv_fragment, sizeof(spirv_fragment));

            VkShaderModule vert = CreateShaderModule(vertWords.data(), vertWords.size() * sizeof(uint32_t));
            VkShaderModule frag = CreateShaderModule(fragWords.data(), fragWords.size() * sizeof(uint32_t));

            VkPipelineShaderStageCreateInfo vertStage{};
            vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
            vertStage.module = vert;
            vertStage.pName  = "main";

            VkPipelineShaderStageCreateInfo fragStage{};
            fragStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragStage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragStage.module = frag;
            fragStage.pName  = "main";

            VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

            VkVertexInputBindingDescription binding{};
            binding.binding   = 0;
            binding.stride    = sizeof(InteropDemo::CudaParticleVertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 3> attrs{};
            attrs[0].location = 0;
            attrs[0].binding  = 0;
            attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
            attrs[0].offset   = offsetof(InteropDemo::CudaParticleVertex, Pos);

            attrs[1].location = 1;
            attrs[1].binding  = 0;
            attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
            attrs[1].offset   = offsetof(InteropDemo::CudaParticleVertex, UV);

            attrs[2].location = 2;
            attrs[2].binding  = 0;
            attrs[2].format   = VK_FORMAT_R8G8B8A8_UNORM;
            attrs[2].offset   = offsetof(InteropDemo::CudaParticleVertex, Color);

            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount   = 1;
            vertexInput.pVertexBindingDescriptions      = &binding;
            vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
            vertexInput.pVertexAttributeDescriptions    = attrs.data();

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.scissorCount  = 1;

            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode             = VK_POLYGON_MODE_FILL;
            raster.cullMode                = VK_CULL_MODE_NONE;
            raster.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth               = 1.0f;
            raster.depthClampEnable        = VK_FALSE;
            raster.rasterizerDiscardEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo msaa{};
            msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.blendEnable         = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

            VkPipelineColorBlendStateCreateInfo colorBlend{};
            colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlend.attachmentCount = 1;
            colorBlend.pAttachments    = &colorBlendAttachment;

            std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
            dynamic.pDynamicStates    = dynamicStates.data();

            const std::array<VkDescriptorSetLayout, 3> setLayouts = {
                m_Set0Layout,
                m_Set1UboLayout,
                m_Set2TextureLayout,
            };

            VkPipelineLayoutCreateInfo layout{};
            layout.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            layout.pSetLayouts    = setLayouts.data();
            CheckVk(vkCreatePipelineLayout(m_Device, &layout, nullptr, &m_PipelineLayout),
                    "vkCreatePipelineLayout");

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount          = 2;
            pipelineInfo.pStages             = stages;
            pipelineInfo.pVertexInputState   = &vertexInput;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState      = &viewportState;
            pipelineInfo.pRasterizationState = &raster;
            pipelineInfo.pMultisampleState   = &msaa;
            pipelineInfo.pColorBlendState    = &colorBlend;
            pipelineInfo.pDynamicState       = &dynamic;
            pipelineInfo.layout              = m_PipelineLayout;
            pipelineInfo.renderPass          = m_RenderPass;
            pipelineInfo.subpass             = 0;

            CheckVk(vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipeline),
                    "vkCreateGraphicsPipelines");

            vkDestroyShaderModule(m_Device, vert, nullptr);
            vkDestroyShaderModule(m_Device, frag, nullptr);
        }

        VkShaderModule CreateShaderModule(const uint32_t* code, size_t byteSize)
        {
            VkShaderModuleCreateInfo info{};
            info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            info.codeSize = byteSize;
            info.pCode    = code;

            VkShaderModule module = VK_NULL_HANDLE;
            CheckVk(vkCreateShaderModule(m_Device, &info, nullptr, &module), "vkCreateShaderModule");
            return module;
        }

        void CreateFramebuffers()
        {
            m_SwapchainFramebuffers.resize(m_SwapchainImageViews.size());
            for (size_t i = 0; i < m_SwapchainImageViews.size(); ++i)
            {
                VkImageView attachments[] = {m_SwapchainImageViews[i]};

                VkFramebufferCreateInfo info{};
                info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                info.renderPass      = m_RenderPass;
                info.attachmentCount = 1;
                info.pAttachments    = attachments;
                info.width           = m_SwapchainExtent.width;
                info.height          = m_SwapchainExtent.height;
                info.layers          = 1;

                CheckVk(vkCreateFramebuffer(m_Device, &info, nullptr, &m_SwapchainFramebuffers[i]),
                        "vkCreateFramebuffer");
            }
        }

        void CreateCommandPoolAndBuffers()
        {
            VkCommandPoolCreateInfo pool{};
            pool.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool.queueFamilyIndex = m_QueueFamilyIndices.GraphicsFamily;
            CheckVk(vkCreateCommandPool(m_Device, &pool, nullptr, &m_CommandPool), "vkCreateCommandPool");

            m_CommandBuffers.resize(m_SwapchainImages.size());
            VkCommandBufferAllocateInfo alloc{};
            alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc.commandPool        = m_CommandPool;
            alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());
            CheckVk(vkAllocateCommandBuffers(m_Device, &alloc, m_CommandBuffers.data()),
                    "vkAllocateCommandBuffers");
        }

        void CreateSyncObjects()
        {
            VkSemaphoreCreateInfo semInfo{};
            semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            CheckVk(vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_ImageAvailable),
                    "vkCreateSemaphore(imageAvailable)");
            CheckVk(vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_RenderFinished),
                    "vkCreateSemaphore(renderFinished)");

            VkExportSemaphoreCreateInfo exportInfo{};
            exportInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
            exportInfo.handleTypes = VulkanExternalExport::VkSemaphoreHandleType();

            VkSemaphoreTypeCreateInfo typeInfo{};
            typeInfo.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            typeInfo.pNext         = &exportInfo;
            typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            typeInfo.initialValue  = 0;

            VkSemaphoreCreateInfo timelineCreate{};
            timelineCreate.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            timelineCreate.pNext = &typeInfo;
            CheckVk(vkCreateSemaphore(m_Device, &timelineCreate, nullptr, &m_TimelineSemaphore),
                    "vkCreateSemaphore(timeline)");

            VkFenceCreateInfo fence{};
            fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            CheckVk(vkCreateFence(m_Device, &fence, nullptr, &m_InFlightFence), "vkCreateFence");
        }

        void CreateSharedVertexBuffer()
        {
            VkExternalMemoryBufferCreateInfo extBufferInfo{};
            extBufferInfo.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
            extBufferInfo.handleTypes = VulkanExternalExport::VkMemoryHandleType();

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.pNext       = &extBufferInfo;
            bufferInfo.size        = static_cast<VkDeviceSize>(sizeof(InteropDemo::CudaParticleVertex)) *
                                     m_MaxParticles * InteropDemo::kVerticesPerParticle;
            bufferInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            CheckVk(vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_SharedVertexBuffer),
                    "vkCreateBuffer(shared)");

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(m_Device, m_SharedVertexBuffer, &req);
            m_SharedAllocationSize = req.size;

            uint32_t memoryTypeIndex = UINT32_MAX;
            try
            {
                memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            }
            catch (...)
            {
                memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            }

            VkExportMemoryAllocateInfo exportAlloc{};
            exportAlloc.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
            exportAlloc.handleTypes = VulkanExternalExport::VkMemoryHandleType();

            VkMemoryAllocateInfo alloc{};
            alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.pNext           = &exportAlloc;
            alloc.allocationSize  = req.size;
            alloc.memoryTypeIndex = memoryTypeIndex;

            CheckVk(vkAllocateMemory(m_Device, &alloc, nullptr, &m_SharedVertexMemory),
                    "vkAllocateMemory(shared)");
            CheckVk(vkBindBufferMemory(m_Device, m_SharedVertexBuffer, m_SharedVertexMemory, 0),
                    "vkBindBufferMemory(shared)");
        }

        void InitializeCudaInterop()
        {
            OwnedInteropHandle memoryHandle   = VulkanExternalExport::ExportMemory(m_Device, m_SharedVertexMemory);
            OwnedInteropHandle semaphoreHandle = VulkanExternalExport::ExportTimelineSemaphore(m_Device, m_TimelineSemaphore);
            m_CudaInterop.Initialize(memoryHandle, semaphoreHandle, m_SharedAllocationSize);
        }

        void RecordCommandBuffer(uint32_t imageIndex, uint32_t particleCount)
        {
            VkCommandBuffer cmd = m_CommandBuffers[imageIndex];

            CheckVk(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer");

            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            CheckVk(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");

            VkClearValue clear{};
            clear.color = {{0.03f, 0.04f, 0.07f, 1.0f}};

            VkRenderPassBeginInfo rp{};
            rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp.renderPass        = m_RenderPass;
            rp.framebuffer       = m_SwapchainFramebuffers[imageIndex];
            rp.renderArea.offset = {0, 0};
            rp.renderArea.extent = m_SwapchainExtent;
            rp.clearValueCount   = 1;
            rp.pClearValues      = &clear;

            vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

            VkViewport viewport{};
            viewport.x        = 0.0f;
            viewport.y        = 0.0f;
            viewport.width    = static_cast<float>(m_SwapchainExtent.width);
            viewport.height   = static_cast<float>(m_SwapchainExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = m_SwapchainExtent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            VkDeviceSize vbOffset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &m_SharedVertexBuffer, &vbOffset);

            const std::array<VkDescriptorSet, 2> descriptorSets = {
                m_UboDescriptorSet,
                m_TextureDescriptorSet,
            };
            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_PipelineLayout,
                                    1,
                                    static_cast<uint32_t>(descriptorSets.size()),
                                    descriptorSets.data(),
                                    0,
                                    nullptr);

            const uint32_t drawVertexCount = particleCount * InteropDemo::kVerticesPerParticle;
            vkCmdDraw(cmd, drawVertexCount, 1, 0, 0);
            vkCmdEndRenderPass(cmd);

            CheckVk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
        }

    private:
        DemoConfig m_Config;
        uint32_t   m_MaxParticles = 200000u;

        GLFWwindow* m_Window = nullptr;

        VkInstance       m_Instance       = VK_NULL_HANDLE;
        VkSurfaceKHR     m_Surface        = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice         m_Device         = VK_NULL_HANDLE;
        VkQueue          m_GraphicsQueue  = VK_NULL_HANDLE;
        VkQueue          m_PresentQueue   = VK_NULL_HANDLE;

        QueueFamilySelection m_QueueFamilyIndices{};

        VkSwapchainKHR              m_Swapchain       = VK_NULL_HANDLE;
        std::vector<VkImage>        m_SwapchainImages;
        std::vector<VkImageView>    m_SwapchainImageViews;
        std::vector<VkFramebuffer>  m_SwapchainFramebuffers;
        VkFormat                    m_SwapchainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D                  m_SwapchainExtent{};

        VkRenderPass      m_RenderPass       = VK_NULL_HANDLE;
        VkPipelineLayout  m_PipelineLayout   = VK_NULL_HANDLE;
        VkPipeline        m_GraphicsPipeline = VK_NULL_HANDLE;

        VkDescriptorSetLayout m_Set0Layout        = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_Set1UboLayout     = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_Set2TextureLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_DescriptorPool     = VK_NULL_HANDLE;
        VkDescriptorSet       m_UboDescriptorSet     = VK_NULL_HANDLE;
        VkDescriptorSet       m_TextureDescriptorSet = VK_NULL_HANDLE;

        VkBuffer       m_TransformBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_TransformMemory = VK_NULL_HANDLE;

        VkImage        m_WhiteImage       = VK_NULL_HANDLE;
        VkDeviceMemory m_WhiteImageMemory = VK_NULL_HANDLE;
        VkImageView    m_WhiteImageView   = VK_NULL_HANDLE;
        VkSampler      m_WhiteSampler     = VK_NULL_HANDLE;

        VkCommandPool               m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_CommandBuffers;

        VkSemaphore m_ImageAvailable   = VK_NULL_HANDLE;
        VkSemaphore m_RenderFinished   = VK_NULL_HANDLE;
        VkSemaphore m_TimelineSemaphore = VK_NULL_HANDLE;
        VkFence     m_InFlightFence    = VK_NULL_HANDLE;

        VkBuffer       m_SharedVertexBuffer   = VK_NULL_HANDLE;
        VkDeviceMemory m_SharedVertexMemory   = VK_NULL_HANDLE;
        size_t         m_SharedAllocationSize = 0;

        uint64_t m_FrameIndex = 0;

        CudaExternalInteropContext m_CudaInterop;
    };

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const DemoConfig config = ParseArgs(argc, argv);

        std::cout << "[InteropDemo] Config: particles=" << config.Particles
                  << ", pcisphIters=" << config.PCISPHIterations
                  << ", calibrate=" << (config.Calibrate ? 1 : 0)
                  << ", durationSec=" << config.DurationSec
                  << ", vsync=" << (config.VSync ? 1 : 0)
                  << ", handleType=" << Engine::CudaInterop::ToString(GetDefaultExternalHandleType()) << "\n";

        CsvLogger logger;
        VulkanDemoApp app(config);
        app.Run(logger);

        std::cout << "[InteropDemo] Completed successfully.\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[InteropDemo] Fatal: " << ex.what() << '\n';
        return 1;
    }
}
