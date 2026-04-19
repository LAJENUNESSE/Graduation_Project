#pragma once
#include <cstdint>
#include <string>

namespace Engine
{

    struct RendererCapabilities
    {
        int         MajorVersion         = 0;
        int         MinorVersion         = 0;
        bool        SupportsComputeShaders = false;
        std::string VendorString;
        std::string RendererString;

        void Query();

        static RendererCapabilities& Get()
        {
            static RendererCapabilities s_Instance;
            return s_Instance;
        }
    };

} // namespace Engine
