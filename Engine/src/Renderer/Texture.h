#pragma once

#include <cstdint>
#include <string>

#include "Core/Base.h"

namespace Engine
{

    enum class TextureFormat
    {
        RGBA8,
        RGBA16F,
        RG16F,
        R32F,
        R16F
    };

    enum class TextureFilter
    {
        Linear,
        Nearest
    };

    enum class TextureWrap
    {
        Repeat,
        ClampToEdge
    };

    enum class TextureDataLayout
    {
        RGBA_U8,
        RGB_Float,
        RGBA_Float
    };

    struct TextureSpecification
    {
        TextureFormat     Format       = TextureFormat::RGBA8;
        TextureFilter     MinFilter    = TextureFilter::Linear;
        TextureFilter     MagFilter    = TextureFilter::Linear;
        TextureWrap       WrapS        = TextureWrap::Repeat;
        TextureWrap       WrapT        = TextureWrap::Repeat;
        TextureDataLayout UploadLayout = TextureDataLayout::RGBA_U8;
    };

    class Texture
    {
    public:
        virtual ~Texture() = default;

        virtual uint32_t GetWidth() const      = 0;
        virtual uint32_t GetHeight() const     = 0;
        virtual uint32_t GetRendererID() const = 0;

        virtual void SetData(void* data, uint32_t size) = 0;

        virtual void Bind(uint32_t slot = 0) const = 0;

        virtual bool operator==(const Texture& other) const = 0;
    };

    class Texture2D : public Texture
    {
    public:
        static Ref<Texture2D> Create(uint32_t width, uint32_t height);
        static Ref<Texture2D> Create(const std::string& path);
        static Ref<Texture2D> Create(const void* data, uint32_t width, uint32_t height);

        // Extended factory with full specification
        static Ref<Texture2D> Create(uint32_t width, uint32_t height, const TextureSpecification& spec);
        static Ref<Texture2D> Create(const void* data, uint32_t width, uint32_t height,
                                      const TextureSpecification& spec);
    };

    class TextureCubemap : public Texture
    {
    public:
        // faces order: +X, -X, +Y, -Y, +Z, -Z
        static Ref<TextureCubemap> Create(const std::vector<std::string>& facePaths);
    };

} // namespace Engine
