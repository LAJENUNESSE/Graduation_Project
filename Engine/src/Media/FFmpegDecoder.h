#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Forward declarations for FFmpeg types
struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;
struct SwrContext;

namespace Engine
{

    class FFmpegDecoder
    {
    public:
        FFmpegDecoder() = default;
        ~FFmpegDecoder();

        bool Open(const std::string& url); // supports rtmp:// and file paths
        void Close();
        bool IsOpen() const { return m_Running.load(); }

        // Video
        int GetVideoWidth() const { return m_VideoWidth; }
        int GetVideoHeight() const { return m_VideoHeight; }
        bool HasNewVideoFrame();
        const uint8_t* GetVideoFrameRGBA(); // Returns current frame RGBA data

        // Audio
        int GetAudioSampleRate() const { return m_AudioSampleRate; }
        int GetAudioChannels() const { return m_AudioChannels; }
        bool HasNewAudioData();
        // Returns audio data and sets sampleCount. Caller should consume immediately.
        const int16_t* GetAudioData(int& sampleCount);

        bool HasVideoStream() const { return m_VideoStreamIdx >= 0; }
        bool HasAudioStream() const { return m_AudioStreamIdx >= 0; }

    private:
        void DecodeLoop();

        std::thread m_DecodeThread;
        std::mutex m_Mutex;
        std::atomic<bool> m_Running{false};

        // FFmpeg contexts
        AVFormatContext* m_FormatCtx = nullptr;
        AVCodecContext* m_VideoCodecCtx = nullptr;
        AVCodecContext* m_AudioCodecCtx = nullptr;
        SwsContext* m_SwsCtx = nullptr;
        SwrContext* m_SwrCtx = nullptr;
        int m_VideoStreamIdx = -1;
        int m_AudioStreamIdx = -1;

        // Video info
        int m_VideoWidth = 0;
        int m_VideoHeight = 0;

        // Audio info
        int m_AudioSampleRate = 0;
        int m_AudioChannels = 0;

        // Triple buffer for video frames
        std::array<std::vector<uint8_t>, 3> m_FrameBuffers;
        std::atomic<int> m_WriteIdx{0};
        std::atomic<int> m_DisplayIdx{-1}; // -1 means no frame ready
        int m_ReadIdx = -1;

        // Audio buffer (lock-protected)
        std::vector<int16_t> m_AudioBuffer;
        bool m_HasNewAudio = false;
        int m_AudioSampleCount = 0;
    };

} // namespace Engine
