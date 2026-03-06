#include "engpch.h"
#include "Audio/AudioClip.h"
#include "Asset/PathUtils.h"
#include "Core/Log.h"

#include <AL/al.h>
namespace Engine
{

    // ---- little-endian helpers ----
    static uint16_t ReadU16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
    static uint32_t ReadU32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

    static bool TagEquals(const uint8_t* p, const char* tag)
    {
        return p[0] == (uint8_t)tag[0] && p[1] == (uint8_t)tag[1] &&
               p[2] == (uint8_t)tag[2] && p[3] == (uint8_t)tag[3];
    }

    AudioClip AudioClip::LoadFromFile(const std::string& path)
    {
        AudioClip clip;
        const std::string resolvedPath = PathUtils::ResolvePathString(path);

        // 1. Open file in binary mode
        std::ifstream file(resolvedPath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            ENGINE_CORE_ERROR("AudioClip: 无法打开文件 '{}'", path);
            return clip;
        }

        auto fileSize = file.tellg();
        if (fileSize < 44) // minimum WAV header size
        {
            ENGINE_CORE_ERROR("AudioClip: 文件太小，不是有效的 WAV 文件 '{}'", path);
            return clip;
        }

        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
        file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
        file.close();

        const uint8_t* data = fileData.data();
        size_t size = fileData.size();

        // 2. Verify RIFF header: "RIFF" + fileSize + "WAVE"
        if (size < 12 || !TagEquals(data, "RIFF") || !TagEquals(data + 8, "WAVE"))
        {
            ENGINE_CORE_ERROR("AudioClip: 不是有效的 RIFF/WAVE 文件 '{}'", path);
            return clip;
        }

        // 3. Scan chunks to find "fmt " and "data"
        bool foundFmt = false;
        bool foundData = false;
        uint16_t audioFormat = 0;
        uint16_t numChannels = 0;
        uint32_t sampleRate = 0;
        uint16_t bitsPerSample = 0;

        size_t offset = 12; // skip past RIFF header
        while (offset + 8 <= size)
        {
            const uint8_t* chunkHeader = data + offset;
            uint32_t chunkSize = ReadU32(chunkHeader + 4);

            if (TagEquals(chunkHeader, "fmt "))
            {
                if (chunkSize < 16 || offset + 8 + 16 > size)
                {
                    ENGINE_CORE_ERROR("AudioClip: fmt 块太小 '{}'", path);
                    return clip;
                }

                const uint8_t* fmtData = chunkHeader + 8;
                audioFormat   = ReadU16(fmtData + 0);
                numChannels   = ReadU16(fmtData + 2);
                sampleRate    = ReadU32(fmtData + 4);
                // byteRate   = ReadU32(fmtData + 8);  // not needed
                // blockAlign = ReadU16(fmtData + 12);  // not needed
                bitsPerSample = ReadU16(fmtData + 14);

                if (audioFormat != 1) // PCM = 1
                {
                    ENGINE_CORE_ERROR("AudioClip: 不支持非 PCM 格式 (audioFormat={}), 文件 '{}'", audioFormat, path);
                    return clip;
                }

                foundFmt = true;
            }
            else if (TagEquals(chunkHeader, "data"))
            {
                if (!foundFmt)
                {
                    // data chunk before fmt chunk -- unusual but technically possible
                    // skip it and hope we find fmt first; we'll re-scan
                }
                else
                {
                    size_t dataStart = offset + 8;
                    size_t dataSize = chunkSize;
                    if (dataStart + dataSize > size)
                        dataSize = size - dataStart; // clamp to file size

                    clip.Data.resize(dataSize);
                    std::memcpy(clip.Data.data(), data + dataStart, dataSize);
                    foundData = true;
                }
            }

            // Advance to next chunk (chunks are 2-byte aligned)
            offset += 8 + chunkSize;
            if (chunkSize & 1) offset++; // padding byte
        }

        // If we found fmt but data came before fmt, do a second scan for the data chunk
        if (foundFmt && !foundData)
        {
            offset = 12;
            while (offset + 8 <= size)
            {
                const uint8_t* chunkHeader = data + offset;
                uint32_t chunkSize = ReadU32(chunkHeader + 4);

                if (TagEquals(chunkHeader, "data"))
                {
                    size_t dataStart = offset + 8;
                    size_t dataSize = chunkSize;
                    if (dataStart + dataSize > size)
                        dataSize = size - dataStart;

                    clip.Data.resize(dataSize);
                    std::memcpy(clip.Data.data(), data + dataStart, dataSize);
                    foundData = true;
                    break;
                }

                offset += 8 + chunkSize;
                if (chunkSize & 1) offset++;
            }
        }

        if (!foundFmt)
        {
            ENGINE_CORE_ERROR("AudioClip: 未找到 fmt 块 '{}'", path);
            return clip;
        }

        if (!foundData)
        {
            ENGINE_CORE_ERROR("AudioClip: 未找到 data 块 '{}'", path);
            return clip;
        }

        // 5. Determine AL format
        if (numChannels == 1 && bitsPerSample == 8)
            clip.ALFormat = AL_FORMAT_MONO8;
        else if (numChannels == 1 && bitsPerSample == 16)
            clip.ALFormat = AL_FORMAT_MONO16;
        else if (numChannels == 2 && bitsPerSample == 8)
            clip.ALFormat = AL_FORMAT_STEREO8;
        else if (numChannels == 2 && bitsPerSample == 16)
            clip.ALFormat = AL_FORMAT_STEREO16;
        else
        {
            ENGINE_CORE_ERROR("AudioClip: 不支持的格式 (channels={}, bitsPerSample={}), 文件 '{}'",
                              numChannels, bitsPerSample, path);
            return clip;
        }

        clip.SampleRate = sampleRate;
        clip.Channels = numChannels;
        clip.BitsPerSample = bitsPerSample;

        ENGINE_CORE_INFO("AudioClip: 已加载 '{}' ({}Hz, {}ch, {}bit, {} 字节 PCM)",
                         path, sampleRate, numChannels, bitsPerSample, clip.Data.size());

        return clip;
    }

} // namespace Engine
