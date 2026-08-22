#include "engpch.h"
#include "Platform/OpenGL/OpenGLAsyncReadback.h"
#include "Debug/GpuMemoryStats.h"
#include "Renderer/StorageBuffer.h"

#include <glad/gl.h>

namespace Engine
{

    OpenGLAsyncReadback::OpenGLAsyncReadback(uint32_t size) : m_Size(size)
    {
        glGenBuffers(1, &m_StagingBuffer);
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_StagingBuffer);
        glBufferData(GL_COPY_WRITE_BUFFER, size, nullptr, GL_STREAM_READ);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }

    OpenGLAsyncReadback::~OpenGLAsyncReadback()
    {
        if (m_Fence)
            glDeleteSync(static_cast<GLsync>(m_Fence));
        if (m_StagingBuffer)
            glDeleteBuffers(1, &m_StagingBuffer);
    }

    void OpenGLAsyncReadback::CopyFrom(const Ref<ShaderStorageBuffer>& src, uint32_t size, uint32_t srcOffset)
    {
        GpuMemoryStats::Get().AddDownloaded(size);

        // Delete previous fence
        if (m_Fence)
        {
            glDeleteSync(static_cast<GLsync>(m_Fence));
            m_Fence = nullptr;
        }

        // Copy SSBO → staging PBO
        glBindBuffer(GL_COPY_READ_BUFFER, src->GetRendererID());
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_StagingBuffer);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcOffset, 0, size);
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

        // Insert fence
        m_Fence   = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        m_Pending = true;
    }

    bool OpenGLAsyncReadback::IsReady() const
    {
        if (!m_Pending || !m_Fence)
            return false;

        GLenum result = glClientWaitSync(static_cast<GLsync>(m_Fence), GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        return (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED);
    }

    void OpenGLAsyncReadback::GetData(void* dest, uint32_t size)
    {
        glBindBuffer(GL_COPY_READ_BUFFER, m_StagingBuffer);
        glGetBufferSubData(GL_COPY_READ_BUFFER, 0, size, dest);
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        m_Pending = false;
    }

    void OpenGLAsyncReadback::Reset()
    {
        if (m_Fence)
        {
            glDeleteSync(static_cast<GLsync>(m_Fence));
            m_Fence = nullptr;
        }
        m_Pending = false;
    }

    bool OpenGLAsyncReadback::IsPending() const
    {
        return m_Pending;
    }

} // namespace Engine
