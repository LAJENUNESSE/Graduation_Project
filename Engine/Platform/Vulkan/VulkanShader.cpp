#include "engpch.h"
#include "Platform/Vulkan/VulkanShader.h"

#include <fstream>
#include <shaderc/shaderc.hpp>

#include "Asset/PathUtils.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanContext.h"

namespace Engine
{

    // ============================================================================
    // VulkanShaderCompiler Implementation
    // ============================================================================

    std::vector<uint32_t> VulkanShaderCompiler::CompileGLSLToSPIRV(const std::string& source, VulkanShaderStage stage,
                                                                   const std::string& shaderName)
    {
        shaderc::Compiler       compiler;
        shaderc::CompileOptions options;

        // Set optimization level
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        // Set target environment
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

        // Map our stage enum to shaderc kind
        shaderc_shader_kind kind;
        switch (stage)
        {
        case VulkanShaderStage::Vertex:
            kind = shaderc_vertex_shader;
            break;
        case VulkanShaderStage::Fragment:
            kind = shaderc_fragment_shader;
            break;
        case VulkanShaderStage::Compute:
            kind = shaderc_compute_shader;
            break;
        default:
            ENGINE_CORE_ERROR("Unknown shader stage!");
            return {};
        }

        // Compile
        shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(source, kind, shaderName.c_str(), options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            ENGINE_CORE_ERROR("Shader compilation failed for '{}': {}", shaderName, result.GetErrorMessage());
            return {};
        }

        // Return SPIR-V bytecode
        return {result.cbegin(), result.cend()};
    }

    VkShaderStageFlagBits VulkanShaderCompiler::GetVkShaderStage(VulkanShaderStage stage)
    {
        switch (stage)
        {
        case VulkanShaderStage::Vertex:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case VulkanShaderStage::Fragment:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case VulkanShaderStage::Compute:
            return VK_SHADER_STAGE_COMPUTE_BIT;
        default:
            ENGINE_CORE_RELEASE_ASSERT(false, "Unknown shader stage!");
            return VK_SHADER_STAGE_VERTEX_BIT;
        }
    }

    // ============================================================================
    // VulkanShader Implementation
    // ============================================================================

    VulkanShader::VulkanShader(const std::string& filepath) : m_FilePath(filepath)
    {
        std::string source        = ReadFile(filepath);
        auto        shaderSources = PreProcess(source);
        CompileShaders(shaderSources);

        // Extract name from filepath
        auto lastSlash = filepath.find_last_of("/\\");
        lastSlash      = lastSlash == std::string::npos ? 0 : lastSlash + 1;
        auto lastDot   = filepath.rfind('.');
        auto count     = lastDot == std::string::npos ? filepath.size() - lastSlash : lastDot - lastSlash;
        m_Name         = filepath.substr(lastSlash, count);
    }

    VulkanShader::VulkanShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
        : m_Name(name)
    {
        std::unordered_map<VulkanShaderStage, std::string> sources;
        sources[VulkanShaderStage::Vertex]   = vertexSrc;
        sources[VulkanShaderStage::Fragment] = fragmentSrc;
        CompileShaders(sources);
    }

    VulkanShader::~VulkanShader()
    {
        Cleanup();
    }

    std::string VulkanShader::ReadFile(const std::string& filepath)
    {
        std::string       result;
        const std::string resolvedPath = PathUtils::ResolvePathString(filepath);
        std::ifstream     in(resolvedPath, std::ios::in | std::ios::binary);
        if (in)
        {
            in.seekg(0, std::ios::end);
            size_t size = in.tellg();
            if (size != static_cast<size_t>(-1))
            {
                result.resize(size);
                in.seekg(0, std::ios::beg);
                in.read(&result[0], size);
            }
            else
            {
                ENGINE_CORE_ERROR("Could not read from file '{0}'", resolvedPath);
            }
        }
        else
        {
            ENGINE_CORE_ERROR("Could not open file '{0}'", resolvedPath);
        }
        return result;
    }

    std::unordered_map<VulkanShaderStage, std::string> VulkanShader::PreProcess(const std::string& source)
    {
        std::unordered_map<VulkanShaderStage, std::string> shaderSources;

        const char* typeToken       = "#type";
        size_t      typeTokenLength = strlen(typeToken);
        size_t      pos             = source.find(typeToken, 0);

        while (pos != std::string::npos)
        {
            size_t eol = source.find_first_of("\r\n", pos);
            ENGINE_CORE_RELEASE_ASSERT(eol != std::string::npos, "Syntax error in shader file");

            size_t      begin = pos + typeTokenLength + 1;
            std::string type  = source.substr(begin, eol - begin);

            // Trim whitespace
            size_t start = type.find_first_not_of(" \t");
            size_t end   = type.find_last_not_of(" \t");
            if (start != std::string::npos)
                type = type.substr(start, end - start + 1);

            VulkanShaderStage stage;
            if (type == "vertex")
                stage = VulkanShaderStage::Vertex;
            else if (type == "fragment" || type == "pixel")
                stage = VulkanShaderStage::Fragment;
            else if (type == "compute")
                stage = VulkanShaderStage::Compute;
            else
            {
                ENGINE_CORE_RELEASE_ASSERT(false, "Unknown shader type!");
                continue;
            }

            size_t nextLinePos = source.find_first_not_of("\r\n", eol);
            ENGINE_CORE_RELEASE_ASSERT(nextLinePos != std::string::npos, "Syntax error in shader file");

            pos = source.find(typeToken, nextLinePos);

            shaderSources[stage] =
                (pos == std::string::npos) ? source.substr(nextLinePos) : source.substr(nextLinePos, pos - nextLinePos);
        }

        return shaderSources;
    }

    void VulkanShader::CompileShaders(const std::unordered_map<VulkanShaderStage, std::string>& shaderSources)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        for (const auto& [stage, source] : shaderSources)
        {
            std::string stageName;
            switch (stage)
            {
            case VulkanShaderStage::Vertex:
                stageName = "vertex";
                break;
            case VulkanShaderStage::Fragment:
                stageName = "fragment";
                break;
            case VulkanShaderStage::Compute:
                stageName = "compute";
                break;
            }

            std::vector<uint32_t> spirv =
                VulkanShaderCompiler::CompileGLSLToSPIRV(source, stage, m_Name + "." + stageName);

            if (spirv.empty())
            {
                ENGINE_CORE_ERROR("Failed to compile {} shader for '{}'", stageName, m_Name);
                continue;
            }

            VkShaderModule shaderModule = CreateShaderModule(spirv);

            switch (stage)
            {
            case VulkanShaderStage::Vertex:
                m_VertexModule = shaderModule;
                break;
            case VulkanShaderStage::Fragment:
                m_FragmentModule = shaderModule;
                break;
            case VulkanShaderStage::Compute:
                m_ComputeModule = shaderModule;
                break;
            }
        }
    }

    VkShaderModule VulkanShader::CreateShaderModule(const std::vector<uint32_t>& spirvCode)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
        createInfo.pCode    = spirvCode.data();

        VkShaderModule shaderModule;
        VkResult       result = vkCreateShaderModule(context->GetDevice(), &createInfo, nullptr, &shaderModule);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan shader module!");

        return shaderModule;
    }

    void VulkanShader::Cleanup()
    {
        auto* context = VulkanContext::Get();
        if (!context)
            return;

        VkDevice device = context->GetDevice();

        if (m_VertexModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device, m_VertexModule, nullptr);
            m_VertexModule = VK_NULL_HANDLE;
        }
        if (m_FragmentModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device, m_FragmentModule, nullptr);
            m_FragmentModule = VK_NULL_HANDLE;
        }
        if (m_ComputeModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device, m_ComputeModule, nullptr);
            m_ComputeModule = VK_NULL_HANDLE;
        }
    }

    void VulkanShader::Bind() const
    {
        // In Vulkan, shader binding is done through pipelines, not directly
        // This is a no-op for compatibility
    }

    void VulkanShader::Unbind() const
    {
        // No-op in Vulkan
    }

    std::vector<VkPipelineShaderStageCreateInfo> VulkanShader::GetShaderStages() const
    {
        std::vector<VkPipelineShaderStageCreateInfo> stages;

        if (m_VertexModule != VK_NULL_HANDLE)
        {
            VkPipelineShaderStageCreateInfo vertStageInfo{};
            vertStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
            vertStageInfo.module = m_VertexModule;
            vertStageInfo.pName  = "main";
            stages.push_back(vertStageInfo);
        }

        if (m_FragmentModule != VK_NULL_HANDLE)
        {
            VkPipelineShaderStageCreateInfo fragStageInfo{};
            fragStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragStageInfo.module = m_FragmentModule;
            fragStageInfo.pName  = "main";
            stages.push_back(fragStageInfo);
        }

        if (m_ComputeModule != VK_NULL_HANDLE)
        {
            VkPipelineShaderStageCreateInfo compStageInfo{};
            compStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            compStageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
            compStageInfo.module = m_ComputeModule;
            compStageInfo.pName  = "main";
            stages.push_back(compStageInfo);
        }

        return stages;
    }

    // Uniform setters - cache values for later descriptor set updates
    void VulkanShader::SetInt(const std::string& name, int value)
    {
        m_UniformCache.ints[name] = value;
    }

    void VulkanShader::SetIntArray(const std::string& name, int* values, uint32_t count)
    {
        // For array uniforms, we'd need a more sophisticated caching mechanism
        // For now, just cache the first value
        if (count > 0)
            m_UniformCache.ints[name] = values[0];
    }

    void VulkanShader::SetFloat(const std::string& name, float value)
    {
        m_UniformCache.floats[name] = value;
    }

    void VulkanShader::SetFloat2(const std::string& name, const glm::vec2& value)
    {
        m_UniformCache.vec2s[name] = value;
    }

    void VulkanShader::SetFloat3(const std::string& name, const glm::vec3& value)
    {
        m_UniformCache.vec3s[name] = value;
    }

    void VulkanShader::SetFloat4(const std::string& name, const glm::vec4& value)
    {
        m_UniformCache.vec4s[name] = value;
    }

    void VulkanShader::SetMat3(const std::string& name, const glm::mat3& value)
    {
        m_UniformCache.mat3s[name] = value;
    }

    void VulkanShader::SetMat4(const std::string& name, const glm::mat4& value)
    {
        m_UniformCache.mat4s[name] = value;
    }

} // namespace Engine
