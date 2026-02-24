#include "engpch.h"
#include "Renderer/Material.h"

#include <glm/gtc/type_ptr.hpp>

namespace Engine
{

    Material::Material(const Ref<Shader>& shader)
        : m_Shader(shader)
    {
    }

    void Material::Bind() const
    {
        m_Shader->Bind();

        for (const auto& [name, value] : m_IntUniforms)
            m_Shader->SetInt(name, value);

        for (const auto& [name, value] : m_FloatUniforms)
            m_Shader->SetFloat(name, value);

        for (const auto& [name, value] : m_Vec2Uniforms)
            m_Shader->SetFloat2(name, value);

        for (const auto& [name, value] : m_Vec3Uniforms)
            m_Shader->SetFloat3(name, value);

        for (const auto& [name, value] : m_Vec4Uniforms)
            m_Shader->SetFloat4(name, value);

        for (const auto& [name, value] : m_Mat4Uniforms)
            m_Shader->SetMat4(name, value);

        for (const auto& [slot, texture] : m_Textures)
        {
            if (texture)
                texture->Bind(slot);
        }
    }

    void Material::Set(const std::string& name, int value)
    {
        m_IntUniforms[name] = value;
    }

    void Material::Set(const std::string& name, float value)
    {
        m_FloatUniforms[name] = value;
    }

    void Material::Set(const std::string& name, const glm::vec2& value)
    {
        m_Vec2Uniforms[name] = value;
    }

    void Material::Set(const std::string& name, const glm::vec3& value)
    {
        m_Vec3Uniforms[name] = value;
    }

    void Material::Set(const std::string& name, const glm::vec4& value)
    {
        m_Vec4Uniforms[name] = value;
    }

    void Material::Set(const std::string& name, const glm::mat4& value)
    {
        m_Mat4Uniforms[name] = value;
    }

    void Material::SetTexture(uint32_t slot, const Ref<Texture2D>& texture)
    {
        m_Textures[slot] = texture;
    }

} // namespace Engine
