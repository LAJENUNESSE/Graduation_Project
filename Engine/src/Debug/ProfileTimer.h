#pragma once

#include <chrono>

namespace Engine
{

    class ProfileTimer
    {
    public:
        ProfileTimer(const char* name, float* outputMs)
            : m_Name(name), m_OutputMs(outputMs), m_StartTime(std::chrono::high_resolution_clock::now())
        {
        }

        ~ProfileTimer()
        {
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration<float, std::milli>(endTime - m_StartTime);
            if (m_OutputMs)
                *m_OutputMs = duration.count();
        }

        // Non-copyable
        ProfileTimer(const ProfileTimer&) = delete;
        ProfileTimer& operator=(const ProfileTimer&) = delete;

    private:
        const char* m_Name;
        float* m_OutputMs;
        std::chrono::high_resolution_clock::time_point m_StartTime;
    };

} // namespace Engine

// Concatenation helper for unique variable names
#define PROFILE_CONCAT_INNER(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_INNER(a, b)

#define PROFILE_SCOPE(name, outputPtr) ::Engine::ProfileTimer PROFILE_CONCAT(profileTimer_, __LINE__)(name, outputPtr)
