#include "Benchmark/FluidBenchmarkLayer.h"

#include "Asset/PathUtils.h"
#include "Core/Application.h"
#include "Core/Log.h"
#include "Debug/GpuMemoryStats.h"
#include "Debug/PerformanceMonitor.h"
#include "Renderer/RendererCapabilities.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace Engine
{

    namespace
    {
        constexpr uint32_t kTimingPrimingSamples = 2;
        constexpr uint32_t kCorrectnessFrames    = 120;

        bool IsFinite(const glm::vec4& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
        }
    } // namespace

    FluidBenchmarkLayer::FluidBenchmarkLayer() : Layer("FluidBenchmarkLayer"), m_Config(FluidBenchmarkConfig::Get()) {}

    void FluidBenchmarkLayer::OnAttach()
    {
        auto& app = Application::Get();
        app.GetWindow().SetVSync(false);
        app.SetFrameRateLimitEnabled(false);
        if (m_Config.Backend == FluidBenchmarkBackend::OpenGL)
            PerformanceMonitor::Get().GetFluidComputeGPUTimer().SetBlockingReadback(true);

        m_Emitter.ParticleCount     = m_Config.ParticleCount;
        m_Emitter.EmitRate          = 0.0f;
        m_Emitter.ParticleLifetime  = 0.0f;
        m_Emitter.PCISPHEnabled     = m_Config.Solver == FluidBenchmarkSolver::PCISPH;
        m_Emitter.PCISPHIterations  = static_cast<int>(m_Config.Iterations);
        m_Emitter.SurfaceTension    = 0.0f;
        m_Emitter.RigidBodyCoupling = false;
        m_Emitter.MeshSDFCoupling   = false;
        m_Emitter.UseBoundary       = true;
        m_Emitter.BoundaryMin       = glm::vec3(-2.0f);
        m_Emitter.BoundaryMax       = glm::vec3(2.0f);

        FluidBenchmarkInitialStateConfig initialConfig;
        initialConfig.ParticleCount   = m_Config.ParticleCount;
        initialConfig.Seed            = m_Config.Seed;
        initialConfig.Spacing         = 0.5f * m_Emitter.SmoothingRadius;
        initialConfig.Center          = glm::vec3(0.0f);
        initialConfig.InitialVelocity = glm::vec3(0.0f);
        m_InitialParticles            = GenerateFluidBenchmarkParticles(initialConfig);
        if (m_InitialParticles.size() != m_Config.ParticleCount)
        {
            Fail("failed to generate deterministic initial particle state");
            return;
        }

        if (!OpenOutput())
            return;

        m_FluidSystem = CreateScope<FluidSystemGPU>(m_Config.ParticleCount, ToComputeBackend(m_Config.Backend));
        m_FluidSystem->Init();
        m_FluidSystem->SetBenchmarkTimingReadback(true);
        if (!m_FluidSystem->IsBackendReady())
        {
            Fail("backend initialization failed: " + m_FluidSystem->GetBackendFailureReason());
            return;
        }
        if (m_FluidSystem->GetActiveBackend() != ToComputeBackend(m_Config.Backend))
        {
            Fail("requested backend was replaced by " +
                 std::string(FluidSystemGPU::BackendLabel(m_FluidSystem->GetActiveBackend())));
            return;
        }

        if (!StartRun())
            return;

        ENGINE_CORE_INFO("[Benchmark] Started {} {}: particles={}, iterations={}, warmup={}, frames={}, runs={}",
                         FluidBenchmarkConfig::BackendLabel(m_Config.Backend),
                         FluidBenchmarkConfig::SolverLabel(m_Config.Solver), m_Config.ParticleCount,
                         m_Config.Iterations, m_Config.WarmupFrames, m_Config.SampleFrames, m_Config.Runs);
    }

    void FluidBenchmarkLayer::OnDetach()
    {
        if (m_Config.Backend == FluidBenchmarkBackend::OpenGL)
            PerformanceMonitor::Get().GetFluidComputeGPUTimer().SetBlockingReadback(false);
        if (m_Output.is_open())
            m_Output.close();
        if (m_FluidSystem)
            m_FluidSystem->SetBenchmarkTimingReadback(false);
        m_FluidSystem.reset();
    }

    void FluidBenchmarkLayer::OnUpdate(Timestep timestep)
    {
        if (m_State == State::PrepareCorrectness)
        {
            if (!m_FluidSystem->SetBenchmarkParticles(m_InitialParticles))
            {
                Fail("failed to reset deterministic state before correctness validation");
                return;
            }
            m_CorrectnessFrames = 0;
            m_State             = State::CorrectnessRun;
            return;
        }
        if (m_State == State::CorrectnessRun)
        {
            m_FluidSystem->Update(m_Config.FixedDeltaTime, glm::vec3(0.0f), m_Emitter, nullptr);
            if (!m_FluidSystem->IsBackendReady() ||
                m_FluidSystem->GetActiveBackend() != ToComputeBackend(m_Config.Backend))
            {
                Fail("backend failed during correctness validation: " + m_FluidSystem->GetBackendFailureReason());
                return;
            }

            ++m_CorrectnessFrames;
            if (m_CorrectnessFrames >= kCorrectnessFrames)
                m_State = State::FinalizeRun;
            return;
        }
        if (m_State == State::FinalizeRun)
        {
            FinalizeRun();
            return;
        }
        if (m_State != State::Running || !m_FluidSystem)
            return;

        m_FluidSystem->Update(m_Config.FixedDeltaTime, glm::vec3(0.0f), m_Emitter, nullptr);

        if (!m_FluidSystem->IsBackendReady() || m_FluidSystem->GetActiveBackend() != ToComputeBackend(m_Config.Backend))
        {
            Fail("backend failed or changed during the benchmark: " + m_FluidSystem->GetBackendFailureReason());
            return;
        }

        const FluidComputeTimingSample& timing = m_FluidSystem->GetLastTimingSample();
        if (!timing.Valid || timing.Sequence == m_LastTimingSequence)
            return;

        m_LastTimingSequence = timing.Sequence;
        if (m_PrimingSamples < kTimingPrimingSamples)
        {
            ++m_PrimingSamples;
            return;
        }
        if (m_WarmupSamples < m_Config.WarmupFrames)
        {
            ++m_WarmupSamples;
            return;
        }

        SampleRow row;
        row.Timestamp         = CurrentTimestamp();
        row.Frame             = static_cast<uint32_t>(m_Samples.size()) + 1;
        row.ComputeMs         = timing.ComputeMs;
        row.InteropMs         = timing.InteropMs;
        row.EndToEndMs        = timestep.GetMilliseconds();
        row.GpuAllocatedBytes = m_RunGpuAllocatedBytes;
        row.WorkingSetBytes   = m_RunWorkingSetBytes;

        // 本采样帧内的传输字节差分
        auto&          gmem    = GpuMemoryStats::Get();
        const uint64_t upNow   = gmem.GetUploadedBytes();
        const uint64_t downNow = gmem.GetDownloadedBytes();
        row.UploadedBytes      = upNow - m_LastUploadedBytes;
        row.DownloadedBytes    = downNow - m_LastDownloadedBytes;
        m_LastUploadedBytes    = upNow;
        m_LastDownloadedBytes  = downNow;

        m_Samples.push_back(std::move(row));

        if (m_Samples.size() >= m_Config.SampleFrames)
            m_State = State::PrepareCorrectness;
    }

    bool FluidBenchmarkLayer::OpenOutput()
    {
        m_OutputPath = PathUtils::ResolvePath(PathUtils::PathFromUtf8(m_Config.OutputPath));

        std::error_code error;
        std::filesystem::create_directories(m_OutputPath.parent_path(), error);
        if (error)
        {
            Fail("cannot create output directory '" + PathUtils::PathToUtf8String(m_OutputPath.parent_path()) +
                 "': " + error.message());
            return false;
        }

        m_Output.open(m_OutputPath, std::ios::out | std::ios::trunc);
        if (!m_Output.is_open())
        {
            Fail("cannot open output file '" + PathUtils::PathToUtf8String(m_OutputPath) + "'");
            return false;
        }

        m_Output << "Timestamp,Backend,Solver,Particles,Iterations,Run,Frame,Warmup,SampleValid,Compute_ms,"
                    "Interop_ms,EndToEnd_ms,AliveCount,MeanDensity,MaxDensityError,RMSDensityError,"
                    "OutOfBoundsCount,StateHash,Device,GpuAllocatedBytes,WorkingSetBytes,UploadedBytes,"
                    "DownloadedBytes\n";
        return true;
    }

    bool FluidBenchmarkLayer::StartRun()
    {
        if (!m_FluidSystem->SetBenchmarkParticles(m_InitialParticles))
        {
            Fail("failed to upload deterministic particle state for run " + std::to_string(m_RunIndex));
            return false;
        }

        m_Samples.clear();
        m_Samples.reserve(m_Config.SampleFrames);
        m_PrimingSamples     = 0;
        m_WarmupSamples      = 0;
        m_LastTimingSequence = 0;

        // 每轮开始抓取内存基线，并把带宽游标对齐到当前累计值
        auto& gmem             = GpuMemoryStats::Get();
        m_RunGpuAllocatedBytes = gmem.TotalAllocatedBytes();
        m_RunWorkingSetBytes   = GpuMemoryStats::QueryProcessWorkingSetBytes();
        m_LastUploadedBytes    = gmem.GetUploadedBytes();
        m_LastDownloadedBytes  = gmem.GetDownloadedBytes();

        m_State = State::Running;
        ENGINE_CORE_INFO("[Benchmark] Run {}/{} initialized", m_RunIndex, m_Config.Runs);
        return true;
    }

    bool FluidBenchmarkLayer::ReadCorrectness(CorrectnessResult& result, std::string& error) const
    {
        result = {};
        std::vector<FluidBenchmarkParticle> particles;
        if (!m_FluidSystem->ReadBenchmarkParticles(particles, error))
            return false;

        double densitySum        = 0.0;
        double densityErrorSqSum = 0.0;
        for (const auto& particle : particles)
        {
            result.Finite = result.Finite && IsFinite(particle.PositionAndLife) &&
                            IsFinite(particle.VelocityAndMaxLife) && IsFinite(particle.Params);
            if (particle.PositionAndLife.w > 0.0f)
                ++result.AliveCount;

            const double density      = static_cast<double>(particle.Params.z);
            const double densityError = std::abs(density - static_cast<double>(m_Emitter.RestDensity));
            densitySum += density;
            densityErrorSqSum += densityError * densityError;
            result.MaxDensityError = std::max(result.MaxDensityError, densityError);

            const glm::vec3 position = glm::vec3(particle.PositionAndLife);
            if (glm::any(glm::lessThan(position, m_Emitter.BoundaryMin)) ||
                glm::any(glm::greaterThan(position, m_Emitter.BoundaryMax)))
            {
                ++result.OutOfBoundsCount;
            }
        }

        if (!particles.empty())
        {
            result.MeanDensity     = densitySum / static_cast<double>(particles.size());
            result.RMSDensityError = std::sqrt(densityErrorSqSum / static_cast<double>(particles.size()));
        }
        result.StateHash = HashFluidBenchmarkParticles(particles);
        return true;
    }

    void FluidBenchmarkLayer::FinalizeRun()
    {
        CorrectnessResult correctness;
        std::string       readError;
        if (!ReadCorrectness(correctness, readError))
        {
            Fail("correctness readback failed in run " + std::to_string(m_RunIndex) + ": " + readError);
            return;
        }
        if (!correctness.Finite || correctness.AliveCount != m_Config.ParticleCount)
        {
            Fail("correctness validation failed in run " + std::to_string(m_RunIndex) +
                 ": finite=" + (correctness.Finite ? std::string("true") : std::string("false")) +
                 ", alive=" + std::to_string(correctness.AliveCount));
            return;
        }

        WriteRunRows(correctness);
        m_Output.flush();
        if (!m_Output.good())
        {
            Fail("failed while writing benchmark CSV");
            return;
        }

        ENGINE_CORE_INFO("[Benchmark] Run {}/{} complete: samples={}, meanDensity={:.3f}, maxDensityError={:.3f}, "
                         "hash=0x{:016x}",
                         m_RunIndex, m_Config.Runs, m_Samples.size(), correctness.MeanDensity,
                         correctness.MaxDensityError, correctness.StateHash);

        if (m_RunIndex < m_Config.Runs)
        {
            ++m_RunIndex;
            StartRun();
            return;
        }

        m_State = State::Finished;
        ENGINE_CORE_INFO("[Benchmark] Finished successfully: {}", PathUtils::PathToUtf8String(m_OutputPath));
        Application::Get().Close(0);
    }

    void FluidBenchmarkLayer::WriteRunRows(const CorrectnessResult& correctness)
    {
        const std::string device = RendererCapabilities::Get().RendererString;
        m_Output << std::setprecision(9);
        for (const SampleRow& sample : m_Samples)
        {
            m_Output << EscapeCSV(sample.Timestamp) << ',' << FluidBenchmarkConfig::BackendLabel(m_Config.Backend)
                     << ',' << FluidBenchmarkConfig::SolverLabel(m_Config.Solver) << ',' << m_Config.ParticleCount
                     << ',' << m_Config.Iterations << ',' << m_RunIndex << ',' << sample.Frame << ','
                     << m_Config.WarmupFrames << ",1," << sample.ComputeMs << ',' << sample.InteropMs << ','
                     << sample.EndToEndMs << ',' << correctness.AliveCount << ',' << correctness.MeanDensity << ','
                     << correctness.MaxDensityError << ',' << correctness.RMSDensityError << ','
                     << correctness.OutOfBoundsCount << ",0x" << std::hex << std::setw(16) << std::setfill('0')
                     << correctness.StateHash << std::dec << std::setfill(' ') << ',' << EscapeCSV(device) << ','
                     << sample.GpuAllocatedBytes << ',' << sample.WorkingSetBytes << ',' << sample.UploadedBytes << ','
                     << sample.DownloadedBytes << '\n';
        }
    }

    void FluidBenchmarkLayer::Fail(const std::string& reason, int exitCode)
    {
        if (m_State == State::Failed)
            return;
        m_State = State::Failed;
        ENGINE_CORE_ERROR("[Benchmark] {}", reason);
        Application::Get().Close(exitCode);
    }

    FluidComputeBackend FluidBenchmarkLayer::ToComputeBackend(FluidBenchmarkBackend backend)
    {
        switch (backend)
        {
        case FluidBenchmarkBackend::OpenGL:
            return FluidComputeBackend::OpenGL;
        case FluidBenchmarkBackend::CUDA:
            return FluidComputeBackend::CUDA;
        case FluidBenchmarkBackend::Vulkan:
            return FluidComputeBackend::Vulkan;
        default:
            return FluidComputeBackend::Automatic;
        }
    }

    std::string FluidBenchmarkLayer::CurrentTimestamp()
    {
        const auto now  = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm    local{};
#ifdef _WIN32
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif
        std::ostringstream stream;
        stream << std::put_time(&local, "%Y-%m-%dT%H:%M:%S");
        return stream.str();
    }

    std::string FluidBenchmarkLayer::EscapeCSV(const std::string& value)
    {
        if (value.find_first_of(",\"\n\r") == std::string::npos)
            return value;

        std::string escaped = "\"";
        for (char character : value)
        {
            if (character == '\"')
                escaped += "\"\"";
            else
                escaped += character;
        }
        escaped += '\"';
        return escaped;
    }

} // namespace Engine
