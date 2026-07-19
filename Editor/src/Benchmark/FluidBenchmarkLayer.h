#pragma once

#include "Core/FluidBenchmarkConfig.h"
#include "Core/Layer.h"
#include "Renderer/FluidBenchmarkData.h"
#include "Renderer/FluidSystemGPU.h"
#include "Scene/Components.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Engine
{

    class FluidBenchmarkLayer final : public Layer
    {
    public:
        FluidBenchmarkLayer();

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Timestep timestep) override;

    private:
        enum class State
        {
            Running,
            FinalizeRun,
            Finished,
            Failed
        };

        struct SampleRow
        {
            std::string Timestamp;
            uint32_t    Frame      = 0;
            float       ComputeMs  = 0.0f;
            float       InteropMs  = 0.0f;
            float       EndToEndMs = 0.0f;
        };

        struct CorrectnessResult
        {
            uint32_t AliveCount       = 0;
            double   MeanDensity      = 0.0;
            double   MaxDensityError  = 0.0;
            double   RMSDensityError  = 0.0;
            uint32_t OutOfBoundsCount = 0;
            uint64_t StateHash        = 0;
            bool     Finite           = true;
        };

        bool              OpenOutput();
        bool              StartRun();
        CorrectnessResult ReadCorrectness() const;
        void              FinalizeRun();
        void              WriteRunRows(const CorrectnessResult& correctness);
        void              Fail(const std::string& reason, int exitCode = 3);

        static FluidComputeBackend ToComputeBackend(FluidBenchmarkBackend backend);
        static std::string         CurrentTimestamp();
        static std::string         EscapeCSV(const std::string& value);

    private:
        FluidBenchmarkConfig                m_Config;
        FluidEmitterComponent               m_Emitter;
        std::vector<FluidBenchmarkParticle> m_InitialParticles;
        Scope<FluidSystemGPU>               m_FluidSystem;
        std::ofstream                       m_Output;
        std::filesystem::path               m_OutputPath;
        std::vector<SampleRow>              m_Samples;
        State                               m_State              = State::Running;
        uint32_t                            m_RunIndex           = 1;
        uint32_t                            m_PrimingSamples     = 0;
        uint32_t                            m_WarmupSamples      = 0;
        uint64_t                            m_LastTimingSequence = 0;
    };

} // namespace Engine
