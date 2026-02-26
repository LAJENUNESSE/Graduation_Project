#include "engpch.h"
#include "Asset/AsyncLoadQueue.h"
#include "Core/Log.h"

#include <stb_image/stb_image.h>

namespace Engine
{

    AsyncLoadQueue::AsyncLoadQueue()
    {
        m_Worker = std::thread(&AsyncLoadQueue::WorkerLoop, this);
    }

    AsyncLoadQueue::~AsyncLoadQueue()
    {
        Shutdown();
    }

    void AsyncLoadQueue::SubmitTexture(const std::string& path, AssetHandle handle)
    {
        {
            std::lock_guard<std::mutex> lock(m_RequestMutex);
            m_Requests.push({path, handle});
        }
        m_RequestCV.notify_one();
    }

    std::vector<TextureCPUData> AsyncLoadQueue::PollResults()
    {
        std::vector<TextureCPUData> results;
        std::lock_guard<std::mutex> lock(m_ResultMutex);
        while (!m_Results.empty())
        {
            results.push_back(std::move(m_Results.front()));
            m_Results.pop();
        }
        return results;
    }

    void AsyncLoadQueue::Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(m_RequestMutex);
            m_Running = false;
        }
        m_RequestCV.notify_one();
        if (m_Worker.joinable())
            m_Worker.join();
    }

    void AsyncLoadQueue::WorkerLoop()
    {
        while (true)
        {
            std::pair<std::string, AssetHandle> request;

            {
                std::unique_lock<std::mutex> lock(m_RequestMutex);
                m_RequestCV.wait(lock, [this] { return !m_Requests.empty() || !m_Running; });

                if (!m_Running && m_Requests.empty())
                    return;

                request = std::move(m_Requests.front());
                m_Requests.pop();
            }

            // CPU-side image decoding (no GL calls here)
            int width, height, channels;
            stbi_set_flip_vertically_on_load(1);
            stbi_uc* data = stbi_load(request.first.c_str(), &width, &height, &channels, 4);

            TextureCPUData result;
            result.Path = request.first;
            result.TargetHandle = request.second;

            if (data)
            {
                result.Width = width;
                result.Height = height;
                size_t size = static_cast<size_t>(width) * height * 4;
                result.Pixels.assign(data, data + size);
                stbi_image_free(data);
            }
            else
            {
                ENGINE_CORE_ERROR("AsyncLoadQueue: Failed to load '{0}'", request.first);
                // 1x1 magenta fallback
                result.Width = 1;
                result.Height = 1;
                result.Pixels = {0xFF, 0x00, 0xFF, 0xFF};
            }

            {
                std::lock_guard<std::mutex> lock(m_ResultMutex);
                m_Results.push(std::move(result));
            }
        }
    }

} // namespace Engine
