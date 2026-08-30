#include "engpch.h"
#include "Platform/OpenGL/OpenGLUniformBuffer.h"

#include "Debug/GpuMemoryStats.h"

#include <glad/gl.h>

namespace Engine
{

    OpenGLUniformBuffer::OpenGLUniformBuffer(uint32_t size, uint32_t binding)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_UNIFORM_BUFFER, m_RendererID);
        glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, m_RendererID);
    }

    OpenGLUniformBuffer::~OpenGLUniformBuffer()
    {
        glDeleteBuffers(1, &m_RendererID);
    }

    void OpenGLUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        GpuMemoryStats::Get().AddUploaded(size);
        glBindBuffer(GL_UNIFORM_BUFFER, m_RendererID);
        glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
    }

    void OpenGLUniformBuffer::Bind(uint32_t binding) const
    {
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, m_RendererID);
    }

} // namespace Engine
