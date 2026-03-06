#pragma once

#include "Core/Application.h"
#include "Core/Log.h"
#include "Core/CrashHandler.h"
#include "Asset/PathUtils.h"

#include <filesystem>
#ifdef _WIN32
#include <Windows.h>
#endif

extern Engine::Application* Engine::CreateApplication();

static void InitializeProjectRootFromExecutable()
{
#ifdef _WIN32
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
        return;

    std::filesystem::path dir = std::filesystem::path(exePath).parent_path();
    if (!Engine::PathUtils::DiscoverProjectRoot(dir))
    {
        Engine::PathUtils::SetProjectRoot(dir);
        printf("[EntryPoint] Failed to discover project root from executable, fallback to executable directory: %s\n",
               Engine::PathUtils::GetProjectRoot().string().c_str());
    }
#endif
}

int main(int argc, char** argv)
{
    Engine::Log::Init();
    InitializeProjectRootFromExecutable();
    Engine::CrashHandler::Install();
    ENGINE_CORE_INFO("Initialized Log!");

    auto app = Engine::CreateApplication();
    app->Run();
    delete app;

    return 0;
}


