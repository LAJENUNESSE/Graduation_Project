#pragma once

#include "Asset/PathUtils.h"
#include "Core/Application.h"
#include "Core/CrashHandler.h"
#include "Core/Log.h"

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

    auto app = Engine::CreateApplication();
    app->Run();

    return 0;
}