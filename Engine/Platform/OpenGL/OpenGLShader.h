#pragma once

#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include "Renderer/Shader.h"

namespace Engine
{

    class OpenGLShader : public Shader
    {
    public:
        OpenGLShader(const std::string& filepath);
        OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
        ~OpenGLShader() override;

        void Bind() const override;
        void Unbind() const override;

        void SetInt(const std::string& name, int value) override;
        void SetIntArray(const std::string& name, int* values, uint32_t count) override;
        void SetFloat(const std::string& name, float value) override;
        void SetFloat2(const std::string& name, const glm::vec2& value) override;
        void SetFloat3(const std::string& name, const glm::vec3& value) override;
        void SetFloat4(const std::string& name, const glm::vec4& value) override;
        void SetMat3(const std::string& name, const glm::mat3& value) override;
        void SetMat4(const std::string& name, const glm::mat4& value) override;

        const std::string& GetName() const override { return m_Name; }

    private:
        std::string ReadFile(const std::string& filepath);
        std::unordered_map<unsigned int, std::string> PreProcess(const std::string& source);
        void Compile(const std::unordered_map<unsigned int, std::string>& shaderSources);

        void UploadUniformInt(const std::string& name, int value);
        void UploadUniformIntArray(const std::string& name, int* values, uint32_t count);
        void UploadUniformFloat(const std::string& name, float value);
        void UploadUniformFloat2(const std::string& name, const glm::vec2& value);
        void UploadUniformFloat3(const std::string& name, const glm::vec3& value);
        void UploadUniformFloat4(const std::string& name, const glm::vec4& value);
        void UploadUniformMat3(const std::string& name, const glm::mat3& matrix);
        void UploadUniformMat4(const std::string& name, const glm::mat4& matrix);

    private:
        uint32_t m_RendererID = 0;
        std::string m_Name;
        std::string m_FilePath;
        mutable std::unordered_map<std::string, int> m_UniformLocationCache;

        int GetUniformLocation(const std::string& name) const;
    };

} // namespace Engine
