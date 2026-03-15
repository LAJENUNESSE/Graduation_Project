#include "engpch.h"
#include "Core/Log.h"
#include "Debug/GPUTimerQuery.h"

#include <glad/gl.h>

#include <cstdlib>
#include <cstring>

namespace Engine
{

    namespace
    {
        bool ContainsToken(const char* str, const char* token)
        {
            return str && token && std::strstr(str, token) != nullptr;
        }
    } // namespace

    GPUTimerQuery::GPUTimerQuery()
    {
        const char* disableEnv = std::getenv("ENGINE_DISABLE_GPU_TIMER");
        bool envDisable = disableEnv && disableEnv[0] == '1';

        const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        bool vmwareDriver = ContainsToken(vendor, "VMware") || ContainsToken(renderer, "SVGA3D");

        m_Disabled = envDisable || vmwareDriver;
        if (m_Disabled)
        {
            ENGINE_WARN("[Perf] GPU timer query disabled (driver compatibility mode).");
            return;
        }

        glGenQueries(2, m_QueryIDs);
    }

    GPUTimerQuery::~GPUTimerQuery()
    {
        // Resources should already be freed by Destroy().
        // Do NOT call glDeleteQueries here — the GL context may be gone.
    }

    void GPUTimerQuery::Destroy()
    {
        if (m_Disabled)
            return;

        if (m_QueryIDs[0] != 0)
        {
            glDeleteQueries(2, m_QueryIDs);
            m_QueryIDs[0] = 0;
            m_QueryIDs[1] = 0;
        }
    }

    void GPUTimerQuery::Begin()
    {
        if (m_Disabled)
            return;

        // Read previous frame's result (if available)
        if (m_FrameCount >= 2)
        {
            int previousIndex = 1 - m_CurrentIndex;
            GLuint available = 0;
            glGetQueryObjectuiv(m_QueryIDs[previousIndex], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_TRUE)
            {
                GLuint64 elapsed = 0;
                glGetQueryObjectui64v(m_QueryIDs[previousIndex], GL_QUERY_RESULT, &elapsed);
                m_ElapsedMs = static_cast<float>(elapsed) / 1000000.0f; // ns -> ms
            }
        }

        glBeginQuery(GL_TIME_ELAPSED, m_QueryIDs[m_CurrentIndex]);
        m_QueryActive = true;
    }

    void GPUTimerQuery::End()
    {
        if (m_Disabled)
            return;

        if (m_QueryActive)
        {
            glEndQuery(GL_TIME_ELAPSED);
            m_CurrentIndex = 1 - m_CurrentIndex; // Swap buffer
            m_QueryActive = false;
            m_FrameCount++;
        }
    }

} // namespace Engine
