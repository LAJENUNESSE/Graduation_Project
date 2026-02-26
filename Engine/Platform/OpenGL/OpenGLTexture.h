#pragma once

#include <string>
#include <cstdint>

#include "Renderer/Texture.h"

namespace Engine
{

    class OpenGLTexture2D : public Texture2D
    {
    public:
        OpenGLTexture2D(uint32_t width, uint32_t height);
        OpenGLTexture2D(const std::string& path);
        OpenGLTexture2D(const void* data, uint32_t width, uint32_t height);
        ~OpenGLTexture2D() override;

        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        uint32_t GetRendererID() const override { return m_RendererID; }

        void SetData(void* data, uint32_t size) override;

        void Bind(uint32_t slot = 0) const override;

        bool operator==(const Texture& other) const override
        {
            return m_RendererID == static_cast<const OpenGLTexture2D&>(other).m_RendererID;
        }

    private:
        std::string m_Path;
        uint32_t m_Width;
        uint32_t m_Height;
        uint32_t m_RendererID;
        unsigned int m_InternalFormat;
        unsigned int m_DataFormat;
    };

    class OpenGLTextureCubemap : public TextureCubemap
    {
    public:
        OpenGLTextureCubemap(const std::vector<std::string>& facePaths);
        ~OpenGLTextureCubemap() override;

        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        uint32_t GetRendererID() const override { return m_RendererID; }

        void SetData(void* data, uint32_t size) override {}
        void Bind(uint32_t slot = 0) const override;

        bool operator==(const Texture& other) const override
        {
            return m_RendererID == static_cast<const OpenGLTextureCubemap&>(other).m_RendererID;
        }

    private:
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        uint32_t m_RendererID = 0;
    };

} // namespace Engine
