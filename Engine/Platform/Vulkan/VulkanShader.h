#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Renderer/Shader.h"

namespace Engine
{

    class VulkanShader : public Shader
    {
    public:
        explicit VulkanShader(const std::string& filepath);
        VulkanShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
        ~VulkanShader() override = default;

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
        void CompileFromFile(const std::string& filepath);
        void CompileFromSourceMap(const std::unordered_map<std::string, std::string>& stageSources,
                                  const std::string&                                  debugName);

    private:
        std::string                                            m_Name;
        std::string                                            m_FilePath;
        std::unordered_map<std::string, std::vector<uint32_t>> m_StageSpirv;
    };

} // namespace Engine
