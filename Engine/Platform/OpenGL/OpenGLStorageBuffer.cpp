#include "engpch.h"
#include "Platform/OpenGL/OpenGLStorageBuffer.h"
#include "Core/Assert.h"

#include <glad/gl.h>

namespace Engine
{

    OpenGLStorageBuffer::OpenGLStorageBuffer(uint32_t size, uint32_t binding) : m_Size(size)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
        glBufferData(GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    OpenGLStorageBuffer::OpenGLStorageBuffer(const void* data, uint32_t size, uint32_t binding) : m_Size(size)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
        glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    OpenGLStorageBuffer::OpenGLStorageBuffer(uint32_t size, uint32_t binding, bool gpuOnly)
        : m_Size(size), m_Immutable(gpuOnly)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
        if (gpuOnly)
            glBufferStorage(GL_SHADER_STORAGE_BUFFER, size, nullptr, 0);
        else
            glBufferData(GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    OpenGLStorageBuffer::OpenGLStorageBuffer(const void* data, uint32_t size, uint32_t binding, bool gpuOnly)
        : m_Size(size), m_Immutable(gpuOnly)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
        if (gpuOnly)
            glBufferStorage(GL_SHADER_STORAGE_BUFFER, size, data, 0);
        else
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
        m_LastBinding = binding;
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_RendererID);
    }

    void OpenGLStorageBuffer::Unbind() const
    {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, m_LastBinding, 0);
    }

    void OpenGLStorageBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        ENGINE_CORE_ASSERT(offset + size <= m_Size, "StorageBuffer::SetData out of bounds");
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void OpenGLStorageBuffer::GetData(void* data, uint32_t size, uint32_t offset) const
    {
        ENGINE_CORE_ASSERT(offset + size <= m_Size, "StorageBuffer::GetData out of bounds");
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

} // namespace Engine
