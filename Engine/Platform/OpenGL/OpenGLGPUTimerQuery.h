#pragma once

#include "Debug/GPUTimerQuery.h"

#include <glad/gl.h>

namespace Engine
{

    // OpenGL 后端 GPUTimerQuery 实现——原 GPUTimerQuery 具体类逐行迁移，行为不变。
    // 双缓冲 glBeginQuery(GL_TIME_ELAPSED)：帧 N 写 query[current]，帧 N+1 的
    // Begin 读 query[previous]（非阻塞 AVAILABILITY 轮询，1 帧延迟零 stall）。
    class OpenGLGPUTimerQuery : public GPUTimerQuery
    {
    public:
        OpenGLGPUTimerQuery();
        ~OpenGLGPUTimerQuery() override;

        void Destroy() override;
        void Begin() override;
        void End() override;

    private:
        uint32_t m_QueryIDs[2]  = {0, 0};
        int      m_CurrentIndex = 0;
        bool     m_QueryActive  = false; // Is there an active query in End() waiting?
        int      m_FrameCount   = 0;     // Track frames to know when results are available
    };

} // namespace Engine
