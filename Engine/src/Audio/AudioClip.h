#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{

    struct AudioClip
    {
        std::vector<uint8_t> Data; // Raw PCM data
        uint32_t SampleRate = 0;
        uint16_t Channels = 0;
        uint16_t BitsPerSample = 0;
        uint32_t ALFormat = 0; // AL_FORMAT_MONO8/16, AL_FORMAT_STEREO8/16

        bool IsValid() const { return !Data.empty() && SampleRate > 0; }

        static AudioClip LoadFromFile(const std::string& path);
    };

} // namespace Engine
