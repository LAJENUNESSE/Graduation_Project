#pragma once

#include "Asset/PathUtils.h"
#include "Core/Application.h"
#include "Core/CrashHandler.h"
#include "Core/FluidBenchmarkConfig.h"
#include "Core/Log.h"
#include "Renderer/RendererAPI.h"

#include <cstring>
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

extern Engine::Scope<Engine::Application> Engine::CreateApplication();

static void InitializeProjectRootFromExecutable()
{
    std::filesystem::path exeDir;

#ifdef _WIN32
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
    {
        printf("[EntryPoint] Warning: GetModuleFileNameW failed (error %lu)\n", GetLastError());
        return;
    }
    exeDir = std::filesystem::path(exePath).parent_path();
#elif defined(__linux__)
    char    buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
    {
        printf("[EntryPoint] Warning: readlink(\"/proc/self/exe\") failed\n");
        return;
    }
    buf[len] = '\0';
    exeDir   = std::filesystem::path(buf).parent_path();
#else
    // Unsupported platform - rely on GetFallbackProjectRoot or manual SetProjectRoot
    return;
#endif

    if (!Engine::PathUtils::DiscoverProjectRoot(exeDir))
    {
        Engine::PathUtils::SetProjectRoot(exeDir);
        printf("[EntryPoint] Failed to discover project root from executable, fallback to executable directory: %s\n",
               Engine::PathUtils::GetProjectRoot().string().c_str());
    }
}

int main(int argc, char** argv)
{
    Engine::Log::Init();
    InitializeProjectRootFromExecutable();
    Engine::CrashHandler::Install();
    ENGINE_CORE_INFO("Initialized Log!");

    std::string benchmarkError;
    if (!Engine::FluidBenchmarkConfig::Initialize(argc, argv, benchmarkError))
    {
        ENGINE_CORE_ERROR("Invalid fluid benchmark arguments: {}", benchmarkError);
        ENGINE_CORE_ERROR("Usage: {}", Engine::FluidBenchmarkConfig::Usage());
        return 2;
    }

    const auto& benchmarkConfig = Engine::FluidBenchmarkConfig::Get();
    if (benchmarkConfig.Enabled)
    {
        ENGINE_CORE_INFO("Fluid benchmark requested: backend={}, solver={}, particles={}, iterations={}",
                         Engine::FluidBenchmarkConfig::BackendLabel(benchmarkConfig.Backend),
                         Engine::FluidBenchmarkConfig::SolverLabel(benchmarkConfig.Solver),
                         benchmarkConfig.ParticleCount, benchmarkConfig.Iterations);

        if (benchmarkConfig.Backend == Engine::FluidBenchmarkBackend::Vulkan)
            Engine::RendererAPI::SetAPI(Engine::RendererAPI::API::Vulkan);
    }

    // Parse command-line: --vulkan selects Vulkan backend, --scene <path> opens a scene on startup
    for (int i = 1; !benchmarkConfig.Enabled && i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--vulkan") == 0)
        {
            Engine::RendererAPI::SetAPI(Engine::RendererAPI::API::Vulkan);
            ENGINE_CORE_INFO("Renderer API set to Vulkan via command-line");
        }
        else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
        {
            Engine::Application::s_LaunchScenePath = argv[++i];
            ENGINE_CORE_INFO("Launch scene set via command-line: {}", Engine::Application::s_LaunchScenePath);
        }
    }

    auto app = Engine::CreateApplication();
    app->Run();

    return app->GetExitCode();
}
