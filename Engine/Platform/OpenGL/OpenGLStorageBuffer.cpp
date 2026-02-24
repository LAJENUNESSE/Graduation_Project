#include "engpch.h"
#include "Platform/OpenGL/OpenGLStorageBuffer.h"

#include <glad/gl.h>

namespace Engine
{

    OpenGLStorageBuffer::OpenGLStorageBuffer(uint32_t size, uint32_t binding)
        : m_Size(size)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
        glBufferData(GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    OpenGLStorageBuffer::OpenGLStorageBuffer(const void* data, uint32_t size, uint32_t binding)
        : m_Size(size)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
        glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    OpenGLStorageBuffer::~OpenGLStorageBuffer()
    {
        glDeleteBuffers(1, &m_RendererID);
    }

    void OpenGLStorageBuffer::Bind(uint32_t binding) const
    {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_RendererID);
    }

    void OpenGLStorageBuffer::Unbind() const
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void OpenGLStorageBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void OpenGLStorageBuffer::GetData(void* data, uint32_t size, uint32_t offset) const
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

} // namespace Engine
