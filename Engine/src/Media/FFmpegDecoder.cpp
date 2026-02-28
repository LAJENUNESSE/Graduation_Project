#include "engpch.h"
#include "Media/FFmpegDecoder.h"
#include "Core/Log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace Engine
{

    FFmpegDecoder::~FFmpegDecoder()
    {
        Close();
    }

    bool FFmpegDecoder::Open(const std::string& url)
    {
        // Ensure we're not already open
        if (m_Running.load())
        {
            ENGINE_CORE_WARN("FFmpegDecoder::Open — 解码器已打开，先关闭再重新打开");
            Close();
        }

        // 1. Open input
        int ret = avformat_open_input(&m_FormatCtx, url.c_str(), nullptr, nullptr);
        if (ret < 0)
        {
            char errBuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errBuf, sizeof(errBuf));
            ENGINE_CORE_ERROR("FFmpegDecoder::Open — avformat_open_input 失败: {} ({})", errBuf, url);
            return false;
        }

        // 2. Find stream info
        ret = avformat_find_stream_info(m_FormatCtx, nullptr);
        if (ret < 0)
        {
            char errBuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errBuf, sizeof(errBuf));
            ENGINE_CORE_ERROR("FFmpegDecoder::Open — avformat_find_stream_info 失败: {}", errBuf);
            avformat_close_input(&m_FormatCtx);
            return false;
        }

        // 3. Find best video and audio streams
        m_VideoStreamIdx = av_find_best_stream(m_FormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        m_AudioStreamIdx = av_find_best_stream(m_FormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

        if (m_VideoStreamIdx < 0 && m_AudioStreamIdx < 0)
        {
            ENGINE_CORE_ERROR("FFmpegDecoder::Open — 未找到视频或音频流: {}", url);
            avformat_close_input(&m_FormatCtx);
            return false;
        }

        // 4. Open video codec
        if (m_VideoStreamIdx >= 0)
        {
            AVStream* stream = m_FormatCtx->streams[m_VideoStreamIdx];
            const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!codec)
            {
                ENGINE_CORE_ERROR("FFmpegDecoder::Open — 未找到视频解码器: codec_id={}", (int)stream->codecpar->codec_id);
                m_VideoStreamIdx = -1;
            }
            else
            {
                m_VideoCodecCtx = avcodec_alloc_context3(codec);
                if (!m_VideoCodecCtx)
                {
                    ENGINE_CORE_ERROR("FFmpegDecoder::Open — avcodec_alloc_context3 视频失败");
                    m_VideoStreamIdx = -1;
                }
                else
                {
                    ret = avcodec_parameters_to_context(m_VideoCodecCtx, stream->codecpar);
                    if (ret < 0)
                    {
                        char errBuf[AV_ERROR_MAX_STRING_SIZE];
                        av_strerror(ret, errBuf, sizeof(errBuf));
                        ENGINE_CORE_ERROR("FFmpegDecoder::Open — avcodec_parameters_to_context 视频失败: {}", errBuf);
                        avcodec_free_context(&m_VideoCodecCtx);
                        m_VideoStreamIdx = -1;
                    }
                    else
                    {
                        ret = avcodec_open2(m_VideoCodecCtx, codec, nullptr);
                        if (ret < 0)
                        {
                            char errBuf[AV_ERROR_MAX_STRING_SIZE];
                            av_strerror(ret, errBuf, sizeof(errBuf));
                            ENGINE_CORE_ERROR("FFmpegDecoder::Open — avcodec_open2 视频失败: {}", errBuf);
                            avcodec_free_context(&m_VideoCodecCtx);
                            m_VideoStreamIdx = -1;
                        }
                    }
                }
            }
        }

        // 5. Setup video conversion (SwsContext + triple buffer)
        if (m_VideoStreamIdx >= 0 && m_VideoCodecCtx)
        {
            m_VideoWidth = m_VideoCodecCtx->width;
            m_VideoHeight = m_VideoCodecCtx->height;

            m_SwsCtx = sws_getContext(
                m_VideoWidth, m_VideoHeight, m_VideoCodecCtx->pix_fmt,
                m_VideoWidth, m_VideoHeight, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);

            if (!m_SwsCtx)
            {
                ENGINE_CORE_ERROR("FFmpegDecoder::Open — sws_getContext 失败");
                avcodec_free_context(&m_VideoCodecCtx);
                m_VideoStreamIdx = -1;
                m_VideoWidth = 0;
                m_VideoHeight = 0;
            }
            else
            {
                size_t frameSize = static_cast<size_t>(m_VideoWidth) * m_VideoHeight * 4;
                for (auto& buf : m_FrameBuffers)
                    buf.resize(frameSize, 0);

                ENGINE_CORE_INFO("FFmpegDecoder::Open — 视频流: {}x{}, pix_fmt={}",
                                 m_VideoWidth, m_VideoHeight, (int)m_VideoCodecCtx->pix_fmt);
            }
        }

        // 6. Open audio codec
        if (m_AudioStreamIdx >= 0)
        {
            AVStream* stream = m_FormatCtx->streams[m_AudioStreamIdx];
            const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!codec)
            {
                ENGINE_CORE_ERROR("FFmpegDecoder::Open — 未找到音频解码器: codec_id={}", (int)stream->codecpar->codec_id);
                m_AudioStreamIdx = -1;
            }
            else
            {
                m_AudioCodecCtx = avcodec_alloc_context3(codec);
                if (!m_AudioCodecCtx)
                {
                    ENGINE_CORE_ERROR("FFmpegDecoder::Open — avcodec_alloc_context3 音频失败");
                    m_AudioStreamIdx = -1;
                }
                else
                {
                    ret = avcodec_parameters_to_context(m_AudioCodecCtx, stream->codecpar);
                    if (ret < 0)
                    {
                        char errBuf[AV_ERROR_MAX_STRING_SIZE];
                        av_strerror(ret, errBuf, sizeof(errBuf));
                        ENGINE_CORE_ERROR("FFmpegDecoder::Open — avcodec_parameters_to_context 音频失败: {}", errBuf);
                        avcodec_free_context(&m_AudioCodecCtx);
                        m_AudioStreamIdx = -1;
                    }
                    else
                    {
                        ret = avcodec_open2(m_AudioCodecCtx, codec, nullptr);
                        if (ret < 0)
                        {
                            char errBuf[AV_ERROR_MAX_STRING_SIZE];
                            av_strerror(ret, errBuf, sizeof(errBuf));
                            ENGINE_CORE_ERROR("FFmpegDecoder::Open — avcodec_open2 音频失败: {}", errBuf);
                            avcodec_free_context(&m_AudioCodecCtx);
                            m_AudioStreamIdx = -1;
                        }
                    }
                }
            }
        }

        // 7. Setup audio resampling (SwrContext → S16 stereo 44100Hz)
        if (m_AudioStreamIdx >= 0 && m_AudioCodecCtx)
        {
            m_SwrCtx = swr_alloc();
            if (!m_SwrCtx)
            {
                ENGINE_CORE_ERROR("FFmpegDecoder::Open — swr_alloc 失败");
                avcodec_free_context(&m_AudioCodecCtx);
                m_AudioStreamIdx = -1;
            }
            else
            {
                AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
                AVChannelLayout inLayout = m_AudioCodecCtx->ch_layout;

                av_opt_set_chlayout(m_SwrCtx, "in_chlayout",  &inLayout, 0);
                av_opt_set_int(m_SwrCtx, "in_sample_rate",     m_AudioCodecCtx->sample_rate, 0);
                av_opt_set_sample_fmt(m_SwrCtx, "in_sample_fmt", m_AudioCodecCtx->sample_fmt, 0);

                av_opt_set_chlayout(m_SwrCtx, "out_chlayout", &outLayout, 0);
                av_opt_set_int(m_SwrCtx, "out_sample_rate",    44100, 0);
                av_opt_set_sample_fmt(m_SwrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

                ret = swr_init(m_SwrCtx);
                if (ret < 0)
                {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    ENGINE_CORE_ERROR("FFmpegDecoder::Open — swr_init 失败: {}", errBuf);
                    swr_free(&m_SwrCtx);
                    avcodec_free_context(&m_AudioCodecCtx);
                    m_AudioStreamIdx = -1;
                }
                else
                {
                    m_AudioSampleRate = 44100;
                    m_AudioChannels = 2;

                    ENGINE_CORE_INFO("FFmpegDecoder::Open — 音频流: 输入 {}Hz {}ch → 输出 44100Hz 2ch S16",
                                     m_AudioCodecCtx->sample_rate, m_AudioCodecCtx->ch_layout.nb_channels);
                }
            }
        }

        // Check we still have at least one usable stream after all setup
        if (m_VideoStreamIdx < 0 && m_AudioStreamIdx < 0)
        {
            ENGINE_CORE_ERROR("FFmpegDecoder::Open — 所有流初始化失败: {}", url);
            avformat_close_input(&m_FormatCtx);
            return false;
        }

        // 8. Start decode thread
        m_Running.store(true);
        m_DecodeThread = std::thread(&FFmpegDecoder::DecodeLoop, this);

        ENGINE_CORE_INFO("FFmpegDecoder::Open — 成功打开: {}", url);
        return true;
    }

    void FFmpegDecoder::Close()
    {
        // 1. Signal thread to stop
        m_Running.store(false);

        // 2. Join thread
        if (m_DecodeThread.joinable())
            m_DecodeThread.join();

        // 3. Free FFmpeg contexts
        if (m_SwsCtx)
        {
            sws_freeContext(m_SwsCtx);
            m_SwsCtx = nullptr;
        }

        if (m_SwrCtx)
        {
            swr_free(&m_SwrCtx);
            m_SwrCtx = nullptr;
        }

        if (m_VideoCodecCtx)
        {
            avcodec_free_context(&m_VideoCodecCtx);
            m_VideoCodecCtx = nullptr;
        }

        if (m_AudioCodecCtx)
        {
            avcodec_free_context(&m_AudioCodecCtx);
            m_AudioCodecCtx = nullptr;
        }

        if (m_FormatCtx)
        {
            avformat_close_input(&m_FormatCtx);
            m_FormatCtx = nullptr;
        }

        // 4. Reset state
        m_VideoStreamIdx = -1;
        m_AudioStreamIdx = -1;
        m_VideoWidth = 0;
        m_VideoHeight = 0;
        m_AudioSampleRate = 0;
        m_AudioChannels = 0;

        for (auto& buf : m_FrameBuffers)
            buf.clear();
        m_WriteIdx.store(0);
        m_DisplayIdx.store(-1);
        m_ReadIdx = -1;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_AudioBuffer.clear();
            m_HasNewAudio = false;
            m_AudioSampleCount = 0;
        }
    }

    void FFmpegDecoder::DecodeLoop()
    {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        AVFrame* rgbaFrame = av_frame_alloc();

        if (!packet || !frame || !rgbaFrame)
        {
            ENGINE_CORE_ERROR("FFmpegDecoder::DecodeLoop — 分配 AVPacket/AVFrame 失败");
            if (packet)    av_packet_free(&packet);
            if (frame)     av_frame_free(&frame);
            if (rgbaFrame) av_frame_free(&rgbaFrame);
            m_Running.store(false);
            return;
        }

        // Allocate RGBA frame buffer for video conversion
        bool rgbaAllocated = false;
        if (m_VideoCodecCtx)
        {
            int ret = av_image_alloc(rgbaFrame->data, rgbaFrame->linesize,
                                     m_VideoWidth, m_VideoHeight, AV_PIX_FMT_RGBA, 1);
            if (ret < 0)
            {
                char errBuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errBuf, sizeof(errBuf));
                ENGINE_CORE_ERROR("FFmpegDecoder::DecodeLoop — av_image_alloc 失败: {}", errBuf);
            }
            else
            {
                rgbaAllocated = true;
            }
        }

        while (m_Running.load())
        {
            int ret = av_read_frame(m_FormatCtx, packet);
            if (ret < 0)
            {
                if (ret == AVERROR_EOF)
                {
                    ENGINE_CORE_INFO("FFmpegDecoder::DecodeLoop — 流结束 (EOF)");
                }
                else
                {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    ENGINE_CORE_ERROR("FFmpegDecoder::DecodeLoop — av_read_frame 失败: {}", errBuf);
                }
                break;
            }

            if (packet->stream_index == m_VideoStreamIdx && m_VideoCodecCtx && rgbaAllocated)
            {
                // Decode video packet
                ret = avcodec_send_packet(m_VideoCodecCtx, packet);
                if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    ENGINE_CORE_WARN("FFmpegDecoder::DecodeLoop — avcodec_send_packet 视频失败: {}", errBuf);
                }

                while (avcodec_receive_frame(m_VideoCodecCtx, frame) == 0)
                {
                    // Convert to RGBA
                    sws_scale(m_SwsCtx,
                              frame->data, frame->linesize, 0, m_VideoHeight,
                              rgbaFrame->data, rgbaFrame->linesize);

                    // Write to triple buffer (lock-free)
                    int writeIdx = m_WriteIdx.load();
                    memcpy(m_FrameBuffers[writeIdx].data(),
                           rgbaFrame->data[0],
                           static_cast<size_t>(m_VideoWidth) * m_VideoHeight * 4);
                    m_DisplayIdx.store(writeIdx);
                    m_WriteIdx.store((writeIdx + 1) % 3);
                }
            }
            else if (packet->stream_index == m_AudioStreamIdx && m_AudioCodecCtx)
            {
                // Decode audio packet
                ret = avcodec_send_packet(m_AudioCodecCtx, packet);
                if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    ENGINE_CORE_WARN("FFmpegDecoder::DecodeLoop — avcodec_send_packet 音频失败: {}", errBuf);
                }

                while (avcodec_receive_frame(m_AudioCodecCtx, frame) == 0)
                {
                    // Calculate output sample count
                    int outSamples = swr_get_out_samples(m_SwrCtx, frame->nb_samples);
                    if (outSamples <= 0)
                        continue;

                    // Resample to S16 stereo
                    std::vector<int16_t> tempBuf(static_cast<size_t>(outSamples) * 2); // stereo = 2 channels
                    uint8_t* outPtr = reinterpret_cast<uint8_t*>(tempBuf.data());
                    int converted = swr_convert(m_SwrCtx,
                                                &outPtr, outSamples,
                                                (const uint8_t**)frame->data, frame->nb_samples);
                    if (converted < 0)
                    {
                        char errBuf[AV_ERROR_MAX_STRING_SIZE];
                        av_strerror(converted, errBuf, sizeof(errBuf));
                        ENGINE_CORE_WARN("FFmpegDecoder::DecodeLoop — swr_convert 失败: {}", errBuf);
                        continue;
                    }

                    if (converted > 0)
                    {
                        std::lock_guard<std::mutex> lock(m_Mutex);
                        m_AudioBuffer.assign(tempBuf.begin(), tempBuf.begin() + converted * 2);
                        m_AudioSampleCount = converted;
                        m_HasNewAudio = true;
                    }
                }
            }

            av_packet_unref(packet);
        }

        // Cleanup
        if (rgbaAllocated && rgbaFrame->data[0])
            av_freep(&rgbaFrame->data[0]);

        av_frame_free(&frame);
        av_frame_free(&rgbaFrame);
        av_packet_free(&packet);
    }

    bool FFmpegDecoder::HasNewVideoFrame()
    {
        int idx = m_DisplayIdx.load();
        return idx >= 0 && idx != m_ReadIdx;
    }

    const uint8_t* FFmpegDecoder::GetVideoFrameRGBA()
    {
        int idx = m_DisplayIdx.load();
        if (idx < 0)
            return nullptr;
        m_ReadIdx = idx;
        return m_FrameBuffers[idx].data();
    }

    bool FFmpegDecoder::HasNewAudioData()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_HasNewAudio;
    }

    const int16_t* FFmpegDecoder::GetAudioData(int& sampleCount)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_HasNewAudio)
        {
            sampleCount = 0;
            return nullptr;
        }
        m_HasNewAudio = false;
        sampleCount = m_AudioSampleCount;
        return m_AudioBuffer.data();
    }

} // namespace Engine
