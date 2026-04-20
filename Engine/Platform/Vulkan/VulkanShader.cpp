#include "engpch.h"
#include "Platform/Vulkan/VulkanShader.h"

#include <fstream>

#include "Asset/PathUtils.h"
#include "Core/Assert.h"
#include "Core/Log.h"

#if defined(ENGINE_VULKAN_HAS_SHADERC)
#include <shaderc/shaderc.hpp>
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
#if !defined(NDEBUG)
        options.SetGenerateDebugInfo();
#endif

        for (const auto& [stage, source] : stageSources)
        {
            const shaderc_shader_kind     kind   = ShaderKindFromStage(stage);
            shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(source, kind, debugName.c_str(), options);

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

    void VulkanShader::Bind() const {}

    void VulkanShader::Unbind() const {}

    void VulkanShader::SetInt(const std::string& name, int value)
    {
        (void)name;
        (void)value;
    }

    void VulkanShader::SetIntArray(const std::string& name, int* values, uint32_t count)
    {
        (void)name;
        (void)values;
        (void)count;
    }

    void VulkanShader::SetFloat(const std::string& name, float value)
    {
        (void)name;
        (void)value;
    }

    void VulkanShader::SetFloat2(const std::string& name, const glm::vec2& value)
    {
        (void)name;
        (void)value;
    }

    void VulkanShader::SetFloat3(const std::string& name, const glm::vec3& value)
    {
        (void)name;
        (void)value;
    }

    void VulkanShader::SetFloat4(const std::string& name, const glm::vec4& value)
    {
        (void)name;
        (void)value;
    }

    void VulkanShader::SetMat3(const std::string& name, const glm::mat3& value)
    {
        (void)name;
        (void)value;
    }

    void VulkanShader::SetMat4(const std::string& name, const glm::mat4& value)
    {
        (void)name;
        (void)value;
    }

} // namespace Engine
