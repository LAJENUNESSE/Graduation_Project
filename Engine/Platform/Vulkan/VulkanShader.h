#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#include "Renderer/Shader.h"

namespace Engine
{

    // Shader stage types for Vulkan
    enum class VulkanShaderStage
    {
        Vertex,
        Fragment,
        Compute
    };

    // ============================================================================
    // VulkanShaderCompiler - GLSL to SPIR-V compilation utilities
    // ============================================================================
    class VulkanShaderCompiler
    {
    public:
        // Compile GLSL source to SPIR-V bytecode
        // Returns empty vector on failure
        static std::vector<uint32_t> CompileGLSLToSPIRV(const std::string& source, VulkanShaderStage stage,
                                                        const std::string& shaderName);

        // Get Vulkan shader stage flag from our enum
        static VkShaderStageFlagBits GetVkShaderStage(VulkanShaderStage stage);
    };

    // ============================================================================
    // VulkanShader - Vulkan shader implementation
    // ============================================================================
    class VulkanShader : public Shader
    {
    public:
        VulkanShader(const std::string& filepath);
        VulkanShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
        ~VulkanShader() override;

        void Bind() const override;
        void Unbind() const override;

        // Note: In Vulkan, uniforms are set via descriptor sets or push constants,
        // not directly like OpenGL. These methods store values for later binding.
        void SetInt(const std::string& name, int value) override;
        void SetIntArray(const std::string& name, int* values, uint32_t count) override;
        void SetFloat(const std::string& name, float value) override;
        void SetFloat2(const std::string& name, const glm::vec2& value) override;
        void SetFloat3(const std::string& name, const glm::vec3& value) override;
        void SetFloat4(const std::string& name, const glm::vec4& value) override;
        void SetMat3(const std::string& name, const glm::mat3& value) override;
        void SetMat4(const std::string& name, const glm::mat4& value) override;

        const std::string& GetName() const override { return m_Name; }

        // Vulkan-specific accessors
        VkShaderModule GetVertexModule() const { return m_VertexModule; }
        VkShaderModule GetFragmentModule() const { return m_FragmentModule; }
        VkShaderModule GetComputeModule() const { return m_ComputeModule; }

        // Get shader stage create infos for pipeline creation
        std::vector<VkPipelineShaderStageCreateInfo> GetShaderStages() const;

        // Check if this is a compute shader
        bool IsCompute() const { return m_ComputeModule != VK_NULL_HANDLE; }

    private:
        std::string                                        ReadFile(const std::string& filepath);
        std::unordered_map<VulkanShaderStage, std::string> PreProcess(const std::string& source);
        void CompileShaders(const std::unordered_map<VulkanShaderStage, std::string>& shaderSources);
        VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirvCode);
        void           Cleanup();

        std::string    m_Name;
        std::string    m_FilePath;
        VkShaderModule m_VertexModule   = VK_NULL_HANDLE;
        VkShaderModule m_FragmentModule = VK_NULL_HANDLE;
        VkShaderModule m_ComputeModule  = VK_NULL_HANDLE;

        // Cached uniform values (for descriptor set updates)
        // In a full implementation, these would be used to update descriptor sets
        struct UniformCache
        {
            std::unordered_map<std::string, int>       ints;
            std::unordered_map<std::string, float>     floats;
            std::unordered_map<std::string, glm::vec2> vec2s;
            std::unordered_map<std::string, glm::vec3> vec3s;
            std::unordered_map<std::string, glm::vec4> vec4s;
            std::unordered_map<std::string, glm::mat3> mat3s;
            std::unordered_map<std::string, glm::mat4> mat4s;
        } m_UniformCache;
    };

} // namespace Engine
