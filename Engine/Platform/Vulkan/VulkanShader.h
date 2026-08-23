#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "Renderer/Shader.h"

namespace Engine
{

    class VulkanShader : public Shader
    {
    public:
        explicit VulkanShader(const std::string& filepath);
        VulkanShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
        ~VulkanShader() override;

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

        // ---- Phase 7 compute pipeline 支持 ----

        // 返回指定 stage 的 SPIR-V 字节码；不存在时返回空 vector
        const std::vector<uint32_t>& GetSpirv(const std::string& stage) const;
        bool                         HasStage(const std::string& stage) const;

        // 懒创建并缓存 VkShaderModule（按 stage）；调用方不拥有句柄，由 VulkanShader 析构时统一销毁
        VkShaderModule GetOrCreateShaderModule(VkDevice device, const std::string& stage);

        // 反射：descriptor set binding 信息（从 SPIR-V 提取）
        struct ReflectedBinding
        {
            uint32_t           Set     = 0;
            uint32_t           Binding = 0;
            VkDescriptorType   Type    = VK_DESCRIPTOR_TYPE_MAX_ENUM;
            uint32_t           Count   = 1;
            VkShaderStageFlags Stages  = 0;
            std::string        Name;
        };

        // 反射：push constant range（从 SPIR-V 提取）
        struct ReflectedPushConstant
        {
            uint32_t           Offset = 0;
            uint32_t           Size   = 0;
            VkShaderStageFlags Stages = 0;
        };

        const std::vector<ReflectedBinding>&      GetReflectedBindings() const { return m_ReflectedBindings; }
        const std::vector<ReflectedPushConstant>& GetReflectedPushConstants() const { return m_ReflectedPushConstants; }

        // 顶点 stage 实际消费的 input location 集合（反射 stage_inputs）；
        // pipeline 组装时过滤 VAO 中未被消费的 attribute
        const std::vector<uint32_t>& GetVertexInputLocations() const { return m_VertexInputLocations; }

        // ---- uniform 值记录（Phase 8.2 地基）----
        // Vulkan 无全局 uniform 状态机，SetXxx 仅把值缓存到 CPU 侧 map，
        // 供后续 UBO 打包 + descriptor set 写入使用（本阶段不真正上传）。
        const std::unordered_map<std::string, int>&              GetIntUniforms() const { return m_IntUniforms; }
        const std::unordered_map<std::string, std::vector<int>>& GetIntArrayUniforms() const
        {
            return m_IntArrayUniforms;
        }
        const std::unordered_map<std::string, float>&     GetFloatUniforms() const { return m_FloatUniforms; }
        const std::unordered_map<std::string, glm::vec2>& GetFloat2Uniforms() const { return m_Float2Uniforms; }
        const std::unordered_map<std::string, glm::vec3>& GetFloat3Uniforms() const { return m_Float3Uniforms; }
        const std::unordered_map<std::string, glm::vec4>& GetFloat4Uniforms() const { return m_Float4Uniforms; }
        const std::unordered_map<std::string, glm::mat3>& GetMat3Uniforms() const { return m_Mat3Uniforms; }
        const std::unordered_map<std::string, glm::mat4>& GetMat4Uniforms() const { return m_Mat4Uniforms; }

    private:
        void CompileFromFile(const std::string& filepath);
        void CompileFromSourceMap(const std::unordered_map<std::string, std::string>& stageSources,
                                  const std::string&                                  debugName);
        void DestroyShaderModules();
        void ReflectFromSpirv();

    private:
        std::string                                            m_Name;
        std::string                                            m_FilePath;
        std::unordered_map<std::string, std::vector<uint32_t>> m_StageSpirv;
        std::unordered_map<std::string, VkShaderModule>        m_StageModules;
        VkDevice                                               m_ModuleDevice = VK_NULL_HANDLE;
        std::vector<ReflectedBinding>                          m_ReflectedBindings;
        std::vector<ReflectedPushConstant>                     m_ReflectedPushConstants;
        std::vector<uint32_t>                                  m_VertexInputLocations;

        // uniform 值缓存（Phase 8.2 地基；Vulkan 无全局 uniform 状态机）
        std::unordered_map<std::string, int>              m_IntUniforms;
        std::unordered_map<std::string, std::vector<int>> m_IntArrayUniforms;
        std::unordered_map<std::string, float>            m_FloatUniforms;
        std::unordered_map<std::string, glm::vec2>        m_Float2Uniforms;
        std::unordered_map<std::string, glm::vec3>        m_Float3Uniforms;
        std::unordered_map<std::string, glm::vec4>        m_Float4Uniforms;
        std::unordered_map<std::string, glm::mat3>        m_Mat3Uniforms;
        std::unordered_map<std::string, glm::mat4>        m_Mat4Uniforms;
    };

} // namespace Engine
