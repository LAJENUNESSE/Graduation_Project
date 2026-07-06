#include "engpch.h"
#include "Platform/OpenGL/OpenGLTexture.h"

#include <glad/gl.h>
#include <stb_image/stb_image.h>

#include "Asset/PathUtils.h"
#include "Core/Assert.h"
#include "Core/Log.h"
namespace Engine
{

    static GLenum TextureFormatToGL(TextureFormat fmt)
    {
        switch (fmt)
        {
        case TextureFormat::RGBA8:
            return GL_RGBA8;
        case TextureFormat::RGBA16F:
            return GL_RGBA16F;
        case TextureFormat::RG16F:
            return GL_RG16F;
        case TextureFormat::R32F:
            return GL_R32F;
        case TextureFormat::R16F:
            return GL_R16F;
        }
        return GL_RGBA8;
    }

    static GLenum TextureFilterToGL(TextureFilter f)
    {
        return f == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR;
    }

    static GLenum TextureWrapToGL(TextureWrap w)
    {
        return w == TextureWrap::ClampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    }

    static void GetUploadParams(TextureDataLayout layout, GLenum& outFormat, GLenum& outType)
    {
        switch (layout)
        {
        case TextureDataLayout::RGBA_U8:
            outFormat = GL_RGBA;
            outType   = GL_UNSIGNED_BYTE;
            break;
        case TextureDataLayout::RGB_Float:
            outFormat = GL_RGB;
            outType   = GL_FLOAT;
            break;
        case TextureDataLayout::RGBA_Float:
            outFormat = GL_RGBA;
            outType   = GL_FLOAT;
            break;
        }
    }

    void OpenGLTexture2D::InitStorage()
    {
        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        glTexStorage2D(GL_TEXTURE_2D, 1, m_InternalFormat, m_Width, m_Height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height), m_InternalFormat(GL_RGBA8), m_DataFormat(GL_RGBA)
    {
        InitStorage();
    }

    OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
        : m_Path(path), m_Width(0), m_Height(0), m_RendererID(0), m_InternalFormat(GL_RGBA8), m_DataFormat(GL_RGBA)
    {
        int               width, height, channels;
        const std::string resolvedPath = PathUtils::ResolvePathString(path);
        stbi_set_flip_vertically_on_load(1);
        // Force RGBA output — handles 1/2/3/4 channel images uniformly
        stbi_uc* data = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);
        if (!data)
        {
            ENGINE_CORE_ERROR("Failed to load image: {0}", resolvedPath);
            // Create 1x1 magenta fallback
            m_Width  = 1;
            m_Height = 1;
            InitStorage();
            uint32_t magenta = 0xFFFF00FF;
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, m_DataFormat, GL_UNSIGNED_BYTE, &magenta);
            return;
        }

        m_Width  = width;
        m_Height = height;
        InitStorage();
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
    }

    OpenGLTexture2D::OpenGLTexture2D(const void* data, uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height), m_InternalFormat(GL_RGBA8), m_DataFormat(GL_RGBA)
    {
        InitStorage();
        if (data)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
    }

    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height, const TextureSpecification& spec)
        : m_Width(width), m_Height(height),
          m_InternalFormat(TextureFormatToGL(spec.Format)), m_DataFormat(GL_RGBA)
    {
        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        // Use glTexImage2D (mutable) to allow resize via glTexImage2D later
        GLenum uploadFmt = GL_RGBA, uploadType = GL_UNSIGNED_BYTE;
        GetUploadParams(spec.UploadLayout, uploadFmt, uploadType);
        m_DataFormat = uploadFmt;
        glTexImage2D(GL_TEXTURE_2D, 0, m_InternalFormat, width, height, 0, uploadFmt, uploadType, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, TextureFilterToGL(spec.MinFilter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, TextureFilterToGL(spec.MagFilter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, TextureWrapToGL(spec.WrapS));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, TextureWrapToGL(spec.WrapT));
    }

    OpenGLTexture2D::OpenGLTexture2D(const void* data, uint32_t width, uint32_t height,
                                     const TextureSpecification& spec)
        : m_Width(width), m_Height(height),
          m_InternalFormat(TextureFormatToGL(spec.Format)), m_DataFormat(GL_RGBA)
    {
        GLenum uploadFmt = GL_RGBA, uploadType = GL_UNSIGNED_BYTE;
        GetUploadParams(spec.UploadLayout, uploadFmt, uploadType);
        m_DataFormat = uploadFmt;

        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        glTexImage2D(GL_TEXTURE_2D, 0, m_InternalFormat, width, height, 0, uploadFmt, uploadType, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, TextureFilterToGL(spec.MinFilter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, TextureFilterToGL(spec.MagFilter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, TextureWrapToGL(spec.WrapS));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, TextureWrapToGL(spec.WrapT));
    }

    OpenGLTexture2D::~OpenGLTexture2D()
    {
        glDeleteTextures(1, &m_RendererID);
    }

    void OpenGLTexture2D::SetData(void* data, uint32_t size)
    {
        uint32_t expected = m_Width * m_Height * 4;
        ENGINE_CORE_ASSERT(size == expected, "Data must be entire texture!");
        if (size != expected)
        {
            ENGINE_CORE_ERROR("Texture SetData size mismatch: expected {0}, got {1}", expected, size);
            return;
        }
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
    }

    void OpenGLTexture2D::Bind(uint32_t slot) const
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
    }

    // ---- TextureCubemap ----

    OpenGLTextureCubemap::OpenGLTextureCubemap(const std::vector<std::string>& facePaths)
    {
        if (facePaths.size() != 6)
        {
            ENGINE_CORE_ERROR("Cubemap requires exactly 6 face paths, got {0}. Using fallback.", facePaths.size());
            // 创建 1x1 品红 fallback cubemap
            m_Width  = 1;
            m_Height = 1;
            glGenTextures(1, &m_RendererID);
            glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
            uint32_t magenta = 0xFFFF00FF;
            for (int i = 0; i < 6; i++)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             &magenta);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            return;
        }

        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);

        stbi_set_flip_vertically_on_load(0); // Cubemaps should NOT be flipped

        for (int i = 0; i < 6; i++)
        {
            int               width, height, channels;
            const std::string resolvedPath = PathUtils::ResolvePathString(facePaths[i]);
            stbi_uc*          data         = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);
            if (data)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, data);
                if (i == 0)
                {
                    m_Width  = width;
                    m_Height = height;
                }
                stbi_image_free(data);
            }
            else
            {
                ENGINE_CORE_ERROR("Cubemap face failed to load: {}", resolvedPath);
                // 1x1 magenta fallback for this face
                uint32_t magenta = 0xFFFF00FF;
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             &magenta);
            }
        }

        stbi_set_flip_vertically_on_load(1); // Restore for Texture2D

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

    OpenGLTextureCubemap::~OpenGLTextureCubemap()
    {
        glDeleteTextures(1, &m_RendererID);
    }

    void OpenGLTextureCubemap::Bind(uint32_t slot) const
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
    }

    void OpenGLTextureCubemap::SetData(void* /*data*/, uint32_t /*size*/)
    {
        ENGINE_CORE_WARN("TextureCubemap::SetData not implemented");
    }

} // namespace Engine
