#include "engpch.h"
#include "Platform/OpenGL/OpenGLFramebuffer.h"

#include "Core/Assert.h"
#include "Core/Log.h"

#include <glad/gl.h>

namespace Engine
{

    static const uint32_t s_MaxFramebufferSize = 8192;

    namespace Utils
    {

        static bool IsDepthFormat(FramebufferTextureFormat format)
        {
            switch (format)
            {
            case FramebufferTextureFormat::DEPTH24STENCIL8:
            case FramebufferTextureFormat::DEPTH_COMPONENT:
                return true;
            default:
                return false;
            }
        }

        static void CreateTextures(uint32_t* outID, uint32_t count)
        {
            glGenTextures(count, outID);
        }

        static void BindTexture(uint32_t id)
        {
            glBindTexture(GL_TEXTURE_2D, id);
        }

        static void AttachColorTexture(uint32_t id, GLenum internalFormat, GLenum format, GLenum type, uint32_t width,
                                       uint32_t height, int index)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);

            // Integer formats require GL_NEAREST — GL_LINEAR is invalid for integer textures
            GLenum filter = (type == GL_INT || type == GL_UNSIGNED_INT) ? GL_NEAREST : GL_LINEAR;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, id, 0);
        }

        static void AttachDepthTexture(uint32_t id, GLenum format, GLenum attachmentType, uint32_t width,
                                       uint32_t height)
        {
            glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachmentType, GL_RENDERBUFFER, id);
        }

    } // namespace Utils

    OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& spec)
        : m_Specification(spec)
    {
        for (auto& s : m_Specification.Attachments.Attachments)
        {
            if (!Utils::IsDepthFormat(s.TextureFormat))
                m_ColorAttachmentSpecifications.emplace_back(s);
            else
                m_DepthAttachmentSpecification = s;
        }

        Invalidate();
    }

    OpenGLFramebuffer::~OpenGLFramebuffer()
    {
        glDeleteFramebuffers(1, &m_RendererID);
        glDeleteTextures(static_cast<GLsizei>(m_ColorAttachments.size()), m_ColorAttachments.data());
        if (m_DepthIsTexture)
            glDeleteTextures(1, &m_DepthAttachment);
        else
            glDeleteRenderbuffers(1, &m_DepthAttachment);
    }

    void OpenGLFramebuffer::Invalidate()
    {
        if (m_RendererID)
        {
            glDeleteFramebuffers(1, &m_RendererID);
            glDeleteTextures(static_cast<GLsizei>(m_ColorAttachments.size()), m_ColorAttachments.data());
            if (m_DepthIsTexture)
                glDeleteTextures(1, &m_DepthAttachment);
            else
                glDeleteRenderbuffers(1, &m_DepthAttachment);

            m_ColorAttachments.clear();
            m_DepthAttachment = 0;
            m_DepthIsTexture = false;
        }

        glGenFramebuffers(1, &m_RendererID);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

        bool useNewPath = !m_ColorAttachmentSpecifications.empty() ||
                          m_DepthAttachmentSpecification.TextureFormat != FramebufferTextureFormat::None;

        if (useNewPath)
        {
            // --- New multi-attachment path ---

            // Color attachments
            if (!m_ColorAttachmentSpecifications.empty())
            {
                m_ColorAttachments.resize(m_ColorAttachmentSpecifications.size());
                Utils::CreateTextures(m_ColorAttachments.data(),
                                      static_cast<uint32_t>(m_ColorAttachments.size()));

                for (size_t i = 0; i < m_ColorAttachments.size(); i++)
                {
                    Utils::BindTexture(m_ColorAttachments[i]);
                    switch (m_ColorAttachmentSpecifications[i].TextureFormat)
                    {
                    case FramebufferTextureFormat::RGBA8:
                        Utils::AttachColorTexture(m_ColorAttachments[i], GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,
                                                  m_Specification.Width, m_Specification.Height, static_cast<int>(i));
                        break;
                    case FramebufferTextureFormat::RED_INTEGER:
                        Utils::AttachColorTexture(m_ColorAttachments[i], GL_R32I, GL_RED_INTEGER, GL_INT,
                                                  m_Specification.Width, m_Specification.Height, static_cast<int>(i));
                        break;
                    default:
                        break;
                    }
                }
            }

            // Depth attachment
            if (m_DepthAttachmentSpecification.TextureFormat != FramebufferTextureFormat::None)
            {
                switch (m_DepthAttachmentSpecification.TextureFormat)
                {
                case FramebufferTextureFormat::DEPTH24STENCIL8:
                    glGenRenderbuffers(1, &m_DepthAttachment);
                    glBindRenderbuffer(GL_RENDERBUFFER, m_DepthAttachment);
                    Utils::AttachDepthTexture(m_DepthAttachment, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL_ATTACHMENT,
                                              m_Specification.Width, m_Specification.Height);
                    m_DepthIsTexture = false;
                    break;
                case FramebufferTextureFormat::DEPTH_COMPONENT:
                {
                    glGenTextures(1, &m_DepthAttachment);
                    glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_Specification.Width,
                                 m_Specification.Height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
                    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
                    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);
                    m_DepthIsTexture = true;
                    break;
                }
                default:
                    break;
                }
            }

            // Set draw buffers
            if (m_ColorAttachments.size() > 1)
            {
                ENGINE_CORE_ASSERT(m_ColorAttachments.size() <= 4, "Too many color attachments");
                GLenum buffers[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
                                     GL_COLOR_ATTACHMENT3};
                glDrawBuffers(static_cast<GLsizei>(m_ColorAttachments.size()), buffers);
            }
            else if (m_ColorAttachments.empty())
            {
                // Depth-only pass
                glDrawBuffer(GL_NONE);
                glReadBuffer(GL_NONE);
            }
        }
        else
        {
            // --- Legacy single-attachment path (backward compatibility for Sandbox) ---

            m_ColorAttachments.resize(1);
            glGenTextures(1, &m_ColorAttachments[0]);
            glBindTexture(GL_TEXTURE_2D, m_ColorAttachments[0]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width, m_Specification.Height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachments[0], 0);

            // Depth-stencil renderbuffer
            glGenRenderbuffers(1, &m_DepthAttachment);
            glBindRenderbuffer(GL_RENDERBUFFER, m_DepthAttachment);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_Specification.Width,
                                  m_Specification.Height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthAttachment);
        }

        ENGINE_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                           "Framebuffer is incomplete!");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::Bind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glViewport(0, 0, m_Specification.Width, m_Specification.Height);
    }

    void OpenGLFramebuffer::Unbind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0 || width > s_MaxFramebufferSize || height > s_MaxFramebufferSize)
        {
            ENGINE_CORE_WARN("Attempted to resize framebuffer to {0}, {1}", width, height);
            return;
        }

        m_Specification.Width = width;
        m_Specification.Height = height;

        Invalidate();
    }

    int OpenGLFramebuffer::ReadPixel(uint32_t attachmentIndex, int x, int y)
    {
        ENGINE_CORE_ASSERT(attachmentIndex < m_ColorAttachments.size(), "Attachment index out of range");

        // Bounds check — skip if out of range
        if (x < 0 || y < 0 || x >= static_cast<int>(m_Specification.Width) ||
            y >= static_cast<int>(m_Specification.Height))
        {
            return -1;
        }

        // Save and restore GL_READ_FRAMEBUFFER binding
        GLint previousReadFBO;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFBO);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_RendererID);
        glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
        int pixelData = -1;

        if (attachmentIndex < m_ColorAttachmentSpecifications.size() &&
            m_ColorAttachmentSpecifications[attachmentIndex].TextureFormat == FramebufferTextureFormat::RED_INTEGER)
        {
            glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
        }
        else
        {
            // RGBA8 fallback — read red channel as integer approximation
            unsigned char rgba[4];
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            pixelData = static_cast<int>(rgba[0]);
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFBO);
        return pixelData;
    }

    void OpenGLFramebuffer::ClearAttachment(uint32_t index, int value)
    {
        ENGINE_CORE_ASSERT(index < m_ColorAttachments.size(), "Attachment index out of range");

        // Save and restore the currently bound FBO so this is safe to call anywhere
        GLint previousFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

        // Only use glClearBufferiv for integer attachments
        if (index < m_ColorAttachmentSpecifications.size() &&
            m_ColorAttachmentSpecifications[index].TextureFormat == FramebufferTextureFormat::RED_INTEGER)
        {
            glClearBufferiv(GL_COLOR, static_cast<GLint>(index), &value);
        }
        else
        {
            float fValue = static_cast<float>(value);
            float clearColor[4] = {fValue, fValue, fValue, fValue};
            glClearBufferfv(GL_COLOR, static_cast<GLint>(index), clearColor);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
    }

} // namespace Engine
