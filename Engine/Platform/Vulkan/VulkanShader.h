#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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
    };

} // namespace Engine
