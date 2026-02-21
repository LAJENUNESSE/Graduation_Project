#pragma once

#include "Core/Application.h"
#include "Core/Log.h"

extern Engine::Application* Engine::CreateApplication();

int main(int argc, char** argv)
{
    Engine::Log::Init();
    ENGINE_CORE_INFO("Initialized Log!");

    auto app = Engine::CreateApplication();
    app->Run();
    delete app;

    return 0;
}
