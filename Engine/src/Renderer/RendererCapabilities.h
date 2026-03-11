#pragma once
#include <cstdint>

namespace Engine
{

    struct RendererCapabilities
    {
        int GLMajorVersion = 0;
        int GLMinorVersion = 0;
        bool SupportsComputeShaders = false; // 需要 GL >= 4.3

        void Query();

        static RendererCapabilities& Get()
        {
            static RendererCapabilities s_Instance;
            return s_Instance;
        }
    };

} // namespace Engine
