#pragma once

#include "Asset/AssetHandle.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace Engine
{

    struct TextureCPUData
    {
        std::vector<uint8_t> Pixels;
        int Width = 0;
        int Height = 0;
        std::string Path;
        AssetHandle TargetHandle;
    };

    class AsyncLoadQueue
    {
    public:
        AsyncLoadQueue();
        ~AsyncLoadQueue();

        void SubmitTexture(const std::string& path, AssetHandle handle);
        std::vector<TextureCPUData> PollResults();
        void Shutdown();

    private:
        void WorkerLoop();

        std::thread m_Worker;
        std::mutex m_RequestMutex;
        std::mutex m_ResultMutex;
        std::condition_variable m_RequestCV;
        std::queue<std::pair<std::string, AssetHandle>> m_Requests;
        std::queue<TextureCPUData> m_Results;
        bool m_Running = true;
    };

} // namespace Engine
