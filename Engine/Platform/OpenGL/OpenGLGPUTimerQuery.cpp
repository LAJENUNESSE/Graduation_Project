#include "engpch.h"
#include "Debug/GPUTimerQuery.h"

#include <glad/gl.h>

namespace Engine
{

    GPUTimerQuery::GPUTimerQuery()
    {
        glGenQueries(2, m_QueryIDs);
    }

    GPUTimerQuery::~GPUTimerQuery()
    {
        // Resources should already be freed by Destroy().
        // Do NOT call glDeleteQueries here — the GL context may be gone.
    }

    void GPUTimerQuery::Destroy()
    {
        if (m_QueryIDs[0] != 0)
        {
            glDeleteQueries(2, m_QueryIDs);
            m_QueryIDs[0] = 0;
            m_QueryIDs[1] = 0;
        }
    }

    void GPUTimerQuery::Begin()
    {
        // Read previous frame's result (if available)
        if (m_FrameCount >= 2)
        {
            int previousIndex = 1 - m_CurrentIndex;
            GLuint64 elapsed = 0;
            glGetQueryObjectui64v(m_QueryIDs[previousIndex], GL_QUERY_RESULT, &elapsed);
            m_ElapsedMs = static_cast<float>(elapsed) / 1000000.0f; // ns -> ms
        }

        glBeginQuery(GL_TIME_ELAPSED, m_QueryIDs[m_CurrentIndex]);
        m_QueryActive = true;
    }

    void GPUTimerQuery::End()
    {
        if (m_QueryActive)
        {
            glEndQuery(GL_TIME_ELAPSED);
            m_CurrentIndex = 1 - m_CurrentIndex; // Swap buffer
            m_QueryActive = false;
            m_FrameCount++;
        }
    }

} // namespace Engine
