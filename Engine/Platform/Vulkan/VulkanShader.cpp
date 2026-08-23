#include "engpch.h"
#include "Platform/Vulkan/VulkanShader.h"

#include <fstream>

#include "Asset/PathUtils.h"
#include "Core/Assert.h"
#include "Core/Log.h"

#if defined(ENGINE_VULKAN_HAS_SHADERC)
#include <shaderc/shaderc.hpp>
#endif

#if defined(ENGINE_VULKAN_HAS_SPIRV_CROSS)
#include <spirv_cross/spirv_cross.hpp>
#endif

namespace Engine
{

    namespace
    {
        std::string ExtractShaderNameFromPath(const std::string& filepath)
        {
            const size_t lastSlash = filepath.find_last_of("/\\");
            const size_t begin     = (lastSlash == std::string::npos) ? 0 : lastSlash + 1;
            const size_t lastDot   = filepath.rfind('.');
            const size_t end       = (lastDot == std::string::npos || lastDot < begin) ? filepath.size() : lastDot;
            return filepath.substr(begin, end - begin);
        }

        std::string ReadShaderFile(const std::string& filepath)
        {
            std::string       source;
            const std::string resolvedPath = PathUtils::ResolvePathString(filepath);
            std::ifstream     in(resolvedPath, std::ios::in | std::ios::binary);
            if (!in)
            {
                ENGINE_CORE_ERROR("[Vulkan] Could not open shader file '{}'", resolvedPath);
                ENGINE_CORE_RELEASE_ASSERT(false, "Failed to open Vulkan shader file");
                return {};
            }

            in.seekg(0, std::ios::end);
            const size_t size = static_cast<size_t>(in.tellg());
            in.seekg(0, std::ios::beg);

            source.resize(size);
            in.read(source.data(), static_cast<std::streamsize>(size));
            return source;
        }

        std::unordered_map<std::string, std::string> PreProcessShaderStages(const std::string& source)
        {
            std::unordered_map<std::string, std::string> stageSources;

            constexpr const char* typeToken       = "#type";
            constexpr size_t      typeTokenLength = 5;
            size_t                pos             = source.find(typeToken, 0);

            while (pos != std::string::npos)
            {
                const size_t eol = source.find_first_of("\r\n", pos);
                ENGINE_CORE_RELEASE_ASSERT(eol != std::string::npos, "Syntax error in shader file");

                const size_t begin = pos + typeTokenLength + 1;
                std::string  stage = source.substr(begin, eol - begin);
                if (stage == "pixel")
                    stage = "fragment";

                ENGINE_CORE_RELEASE_ASSERT(stage == "vertex" || stage == "fragment" || stage == "compute",
                                           "Unknown shader stage token");

                const size_t nextLinePos = source.find_first_not_of("\r\n", eol);
                ENGINE_CORE_RELEASE_ASSERT(nextLinePos != std::string::npos, "Syntax error in shader file");

                pos                 = source.find(typeToken, nextLinePos);
                stageSources[stage] = (pos == std::string::npos) ? source.substr(nextLinePos)
                                                                 : source.substr(nextLinePos, pos - nextLinePos);
            }

            ENGINE_CORE_RELEASE_ASSERT(!stageSources.empty(), "Shader file does not contain any #type stages");
            return stageSources;
        }

#if defined(ENGINE_VULKAN_HAS_SHADERC)
        shaderc_shader_kind ShaderKindFromStage(const std::string& stage)
        {
            if (stage == "vertex")
                return shaderc_glsl_vertex_shader;
            if (stage == "fragment")
                return shaderc_glsl_fragment_shader;
            if (stage == "compute")
                return shaderc_glsl_compute_shader;

            ENGINE_CORE_RELEASE_ASSERT(false, "Unknown Vulkan shader stage");
            return shaderc_glsl_infer_from_source;
        }
#endif
    } // namespace

    VulkanShader::VulkanShader(const std::string& filepath)
        : m_Name(ExtractShaderNameFromPath(filepath)), m_FilePath(filepath)
    {
        CompileFromFile(filepath);
    }

    VulkanShader::VulkanShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
        : m_Name(name)
    {
        std::unordered_map<std::string, std::string> stageSources;
        stageSources["vertex"]   = vertexSrc;
        stageSources["fragment"] = fragmentSrc;
        CompileFromSourceMap(stageSources, m_Name);
    }

    VulkanShader::~VulkanShader()
    {
        DestroyShaderModules();
    }

    const std::vector<uint32_t>& VulkanShader::GetSpirv(const std::string& stage) const
    {
        static const std::vector<uint32_t> empty;
        auto                               it = m_StageSpirv.find(stage);
        return (it != m_StageSpirv.end()) ? it->second : empty;
    }

    bool VulkanShader::HasStage(const std::string& stage) const
    {
        return m_StageSpirv.find(stage) != m_StageSpirv.end();
    }

    VkShaderModule VulkanShader::GetOrCreateShaderModule(VkDevice device, const std::string& stage)
    {
        ENGINE_CORE_RELEASE_ASSERT(device != VK_NULL_HANDLE, "VulkanShader::GetOrCreateShaderModule: device is null");

        // 不允许跨设备复用
        if (m_ModuleDevice != VK_NULL_HANDLE && m_ModuleDevice != device)
        {
            DestroyShaderModules();
        }
        m_ModuleDevice = device;

        if (auto it = m_StageModules.find(stage); it != m_StageModules.end())
            return it->second;

        const auto spirvIt = m_StageSpirv.find(stage);
        if (spirvIt == m_StageSpirv.end() || spirvIt->second.empty())
            return VK_NULL_HANDLE;

        VkShaderModuleCreateInfo info{};
        info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = spirvIt->second.size() * sizeof(uint32_t);
        info.pCode    = spirvIt->second.data();

        VkShaderModule module = VK_NULL_HANDLE;
        VkResult       result = vkCreateShaderModule(device, &info, nullptr, &module);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create VkShaderModule for VulkanShader stage");

        m_StageModules[stage] = module;
        return module;
    }

    void VulkanShader::DestroyShaderModules()
    {
        if (m_ModuleDevice == VK_NULL_HANDLE)
        {
            m_StageModules.clear();
            return;
        }

        for (auto& [stage, module] : m_StageModules)
        {
            if (module != VK_NULL_HANDLE)
                vkDestroyShaderModule(m_ModuleDevice, module, nullptr);
        }
        m_StageModules.clear();
        m_ModuleDevice = VK_NULL_HANDLE;
    }

    void VulkanShader::CompileFromFile(const std::string& filepath)
    {
        const std::string shaderSource = ReadShaderFile(filepath);
        auto              stageSources = PreProcessShaderStages(shaderSource);
        CompileFromSourceMap(stageSources, filepath);
    }

    void VulkanShader::CompileFromSourceMap(const std::unordered_map<std::string, std::string>& stageSources,
                                            const std::string&                                  debugName)
    {
        ENGINE_CORE_RELEASE_ASSERT(!stageSources.empty(), "Vulkan shader stage sources are empty");

        m_StageSpirv.clear();

#if defined(ENGINE_VULKAN_HAS_SHADERC)
        shaderc::Compiler       compiler;
        shaderc::CompileOptions options;
        options.SetSourceLanguage(shaderc_source_language_glsl);
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        // glslang/shaderc 在 Vulkan 目标环境下会预定义 VULKAN；不要重复注入不同替换值。
#if !defined(NDEBUG)
        options.SetGenerateDebugInfo();
#endif

        for (const auto& [stage, source] : stageSources)
        {
            // 场景 shader 为兼容 OpenGL 4.3 context 统一写 #version 330/400；
            // Vulkan 语义下 layout(binding) 需要 ≥420（ARB_shading_language_420pack），
            // 编译前统一提升到 450，仅影响本路径的内存源码，不改磁盘文件与 OpenGL 路径。
            std::string stageSource = source;
            if (stageSource.rfind("#version 330", 0) == 0 || stageSource.rfind("#version 400", 0) == 0)
                stageSource = "#version 450" + stageSource.substr(stageSource.find('\n'));

            const shaderc_shader_kind     kind = ShaderKindFromStage(stage);
            shaderc::SpvCompilationResult result =
                compiler.CompileGlslToSpv(stageSource, kind, debugName.c_str(), options);

            if (result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                ENGINE_CORE_ERROR("[Vulkan] Shader compile failed '{}' stage '{}': {}", debugName, stage,
                                  result.GetErrorMessage());
                ENGINE_CORE_RELEASE_ASSERT(false, "Vulkan shader compilation failure");
                return;
            }

            m_StageSpirv[stage] = std::vector<uint32_t>(result.cbegin(), result.cend());
        }

        ENGINE_CORE_RELEASE_ASSERT(!m_StageSpirv.empty(), "Vulkan shader compiled to empty SPIR-V outputs");

        ReflectFromSpirv();
#else
        (void)stageSources;
        (void)debugName;

        static bool warnedNoShaderc = false;
        if (!warnedNoShaderc)
        {
            warnedNoShaderc = true;
            ENGINE_CORE_WARN("[Vulkan] shaderc unavailable; VulkanShader runtime GLSL->SPIR-V compile is disabled");
        }
#endif
    }

    void VulkanShader::ReflectFromSpirv()
    {
        m_ReflectedBindings.clear();
        m_ReflectedPushConstants.clear();

#if defined(ENGINE_VULKAN_HAS_SPIRV_CROSS)
        auto stageToVk = [](const std::string& stage) -> VkShaderStageFlags
        {
            if (stage == "vertex")
                return VK_SHADER_STAGE_VERTEX_BIT;
            if (stage == "fragment")
                return VK_SHADER_STAGE_FRAGMENT_BIT;
            if (stage == "compute")
                return VK_SHADER_STAGE_COMPUTE_BIT;
            return 0;
        };

        // 同一个 (set, binding) 可能在多个 stage 共享（vertex + fragment 等），按 key 合并 stage flags
        struct Key
        {
            uint32_t set;
            uint32_t binding;
            bool     operator==(const Key& o) const { return set == o.set && binding == o.binding; }
        };
        struct KeyHash
        {
            size_t operator()(const Key& k) const noexcept { return (static_cast<size_t>(k.set) << 32) ^ k.binding; }
        };
        std::unordered_map<Key, ReflectedBinding, KeyHash> mergedBindings;
        VkShaderStageFlags                                 pushConstantStages = 0;
        uint32_t                                           pushConstantSize   = 0;

        for (const auto& [stage, spirv] : m_StageSpirv)
        {
            if (spirv.empty())
                continue;

            const VkShaderStageFlags stageBit = stageToVk(stage);
            try
            {
                spirv_cross::Compiler        compiler(spirv);
                spirv_cross::ShaderResources res = compiler.get_shader_resources();

                auto addBinding = [&](const spirv_cross::Resource& r, VkDescriptorType type)
                {
                    const spirv_cross::SPIRType& t = compiler.get_type(r.type_id);
                    Key                          key{compiler.get_decoration(r.id, spv::DecorationDescriptorSet),
                            compiler.get_decoration(r.id, spv::DecorationBinding)};
                    auto&                        b = mergedBindings[key];
                    b.Set                          = key.set;
                    b.Binding                      = key.binding;
                    b.Type                         = type;
                    b.Count                        = t.array.empty() ? 1u : t.array[0];
                    b.Stages |= stageBit;
                    if (b.Name.empty())
                        b.Name = r.name;
                };

                for (const auto& r : res.uniform_buffers)
                    addBinding(r, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                for (const auto& r : res.storage_buffers)
                    addBinding(r, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                for (const auto& r : res.sampled_images)
                    addBinding(r, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                for (const auto& r : res.storage_images)
                    addBinding(r, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
                for (const auto& r : res.separate_images)
                    addBinding(r, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
                for (const auto& r : res.separate_samplers)
                    addBinding(r, VK_DESCRIPTOR_TYPE_SAMPLER);

                // Push constants：取所有 stage 中最大 size 作为 range 总长，stage flags 合并
                for (const auto& pc : res.push_constant_buffers)
                {
                    const spirv_cross::SPIRType& t    = compiler.get_type(pc.base_type_id);
                    const uint32_t               size = static_cast<uint32_t>(compiler.get_declared_struct_size(t));
                    pushConstantSize                  = std::max(pushConstantSize, size);
                    pushConstantStages |= stageBit;
                }
            }
            catch (const std::exception& e)
            {
                ENGINE_CORE_ERROR("[Vulkan] SPIR-V reflection failed for '{}' stage '{}': {}", m_Name, stage, e.what());
            }
        }

        m_ReflectedBindings.reserve(mergedBindings.size());
        for (auto& [_, b] : mergedBindings)
            m_ReflectedBindings.push_back(std::move(b));

        if (pushConstantSize > 0)
        {
            ReflectedPushConstant pc{};
            pc.Offset = 0;
            pc.Size   = pushConstantSize;
            pc.Stages = pushConstantStages;
            m_ReflectedPushConstants.push_back(pc);
        }
#endif
    }

    void VulkanShader::Bind() const {}

    void VulkanShader::Unbind() const {}

    void VulkanShader::SetInt(const std::string& name, int value)
    {
        m_IntUniforms[name] = value;
    }

    void VulkanShader::SetIntArray(const std::string& name, int* values, uint32_t count)
    {
        m_IntArrayUniforms[name] = std::vector<int>(values, values + count);
    }

    void VulkanShader::SetFloat(const std::string& name, float value)
    {
        m_FloatUniforms[name] = value;
    }

    void VulkanShader::SetFloat2(const std::string& name, const glm::vec2& value)
    {
        m_Float2Uniforms[name] = value;
    }

    void VulkanShader::SetFloat3(const std::string& name, const glm::vec3& value)
    {
        m_Float3Uniforms[name] = value;
    }

    void VulkanShader::SetFloat4(const std::string& name, const glm::vec4& value)
    {
        m_Float4Uniforms[name] = value;
    }

    void VulkanShader::SetMat3(const std::string& name, const glm::mat3& value)
    {
        m_Mat3Uniforms[name] = value;
    }

    void VulkanShader::SetMat4(const std::string& name, const glm::mat4& value)
    {
        m_Mat4Uniforms[name] = value;
    }

} // namespace Engine
