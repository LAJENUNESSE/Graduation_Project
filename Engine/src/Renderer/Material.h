#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace Engine
{

    class Material
    {
    public:
        Material(const Ref<Shader>& shader);

        void Bind() const;

        void Set(const std::string& name, int value);
        void Set(const std::string& name, float value);
        void Set(const std::string& name, const glm::vec2& value);
        void Set(const std::string& name, const glm::vec3& value);
        void Set(const std::string& name, const glm::vec4& value);
        void Set(const std::string& name, const glm::mat4& value);

        void SetTexture(uint32_t slot, const Ref<Texture2D>& texture);

        Ref<Shader> GetShader() const { return m_Shader; }

    private:
        Ref<Shader> m_Shader;
        std::unordered_map<std::string, int> m_IntUniforms;
        std::unordered_map<std::string, float> m_FloatUniforms;
        std::unordered_map<std::string, glm::vec2> m_Vec2Uniforms;
        std::unordered_map<std::string, glm::vec3> m_Vec3Uniforms;
        std::unordered_map<std::string, glm::vec4> m_Vec4Uniforms;
        std::unordered_map<std::string, glm::mat4> m_Mat4Uniforms;
        std::unordered_map<uint32_t, Ref<Texture2D>> m_Textures;
    };

} // namespace Engine
