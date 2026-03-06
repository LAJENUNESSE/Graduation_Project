#pragma once

#include "Core/Application.h"
#include "Core/Log.h"
#include "Core/CrashHandler.h"

#include <filesystem>
#ifdef _WIN32
#include <Windows.h>
#endif

extern Engine::Application* Engine::CreateApplication();

static void SetWorkingDirectoryToProjectRoot()
{
#ifdef _WIN32
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
        return;

    std::filesystem::path dir = std::filesystem::path(exePath).parent_path();

    for (int i = 0; i < 10; ++i)
    {
        if (std::filesystem::exists(dir / "assets") &&
            std::filesystem::exists(dir / "Editor"))
        {
            std::filesystem::current_path(dir);
            printf("[EntryPoint] Set working directory to: %s\n", dir.string().c_str());
            return;
        }

        auto parent = dir.parent_path();
        if (parent == dir)
            break;
        dir = parent;
    }
#endif
}

int main(int argc, char** argv)
{
    SetWorkingDirectoryToProjectRoot();

    Engine::Log::Init();
    Engine::CrashHandler::Install();
    ENGINE_CORE_INFO("Initialized Log!");

    auto app = Engine::CreateApplication();
    app->Run();
    delete app;

    return 0;
}

