#include "engpch.h"
#include "Platform/OpenGL/OpenGLShader.h"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#include "Asset/PathUtils.h"
#include "Core/Assert.h"
#include "Core/Log.h"
namespace Engine
{

    static unsigned int ShaderTypeFromString(const std::string& type)
    {
        if (type == "vertex")
            return GL_VERTEX_SHADER;
        if (type == "fragment" || type == "pixel")
            return GL_FRAGMENT_SHADER;
        if (type == "compute")
            return GL_COMPUTE_SHADER;

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown shader type!");
        return 0;
    }

    OpenGLShader::OpenGLShader(const std::string& filepath) : m_FilePath(filepath)
    {
        std::string source = ReadFile(filepath);
        auto shaderSources = PreProcess(source);
        Compile(shaderSources);

        // Extract name from filepath
        auto lastSlash = filepath.find_last_of("/\\");
        lastSlash = lastSlash == std::string::npos ? 0 : lastSlash + 1;
        auto lastDot = filepath.rfind('.');
        auto count = lastDot == std::string::npos ? filepath.size() - lastSlash : lastDot - lastSlash;
        m_Name = filepath.substr(lastSlash, count);
    }

    OpenGLShader::OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
        : m_Name(name)
    {
        std::unordered_map<unsigned int, std::string> sources;
        sources[GL_VERTEX_SHADER] = vertexSrc;
        sources[GL_FRAGMENT_SHADER] = fragmentSrc;
        Compile(sources);
    }

    OpenGLShader::~OpenGLShader()
    {
        glDeleteProgram(m_RendererID);
    }

    std::string OpenGLShader::ReadFile(const std::string& filepath)
    {
        std::string result;
        const std::string resolvedPath = PathUtils::ResolvePathString(filepath);
        std::ifstream in(resolvedPath, std::ios::in | std::ios::binary);
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

    std::unordered_map<unsigned int, std::string> OpenGLShader::PreProcess(const std::string& source)
    {
        std::unordered_map<unsigned int, std::string> shaderSources;

        const char* typeToken = "#type";
        size_t typeTokenLength = strlen(typeToken);
        size_t pos = source.find(typeToken, 0);

        while (pos != std::string::npos)
        {
            size_t eol = source.find_first_of("\r\n", pos);
            ENGINE_CORE_RELEASE_ASSERT(eol != std::string::npos, "Syntax error in shader file");

            size_t begin = pos + typeTokenLength + 1;
            std::string type = source.substr(begin, eol - begin);

            size_t nextLinePos = source.find_first_not_of("\r\n", eol);
            ENGINE_CORE_RELEASE_ASSERT(nextLinePos != std::string::npos, "Syntax error in shader file");

            pos = source.find(typeToken, nextLinePos);
            shaderSources[ShaderTypeFromString(type)] =
                (pos == std::string::npos) ? source.substr(nextLinePos) : source.substr(nextLinePos, pos - nextLinePos);
        }

        return shaderSources;
    }

    void OpenGLShader::Compile(const std::unordered_map<unsigned int, std::string>& shaderSources)
    {
        GLuint program = glCreateProgram();
        ENGINE_CORE_RELEASE_ASSERT(shaderSources.size() <= 3, "We only support 3 shaders for now");

        std::array<GLuint, 3> glShaderIDs;
        int glShaderIDIndex = 0;

        for (auto& [type, source] : shaderSources)
        {
            GLuint shader = glCreateShader(type);

            const GLchar* sourceCStr = source.c_str();
            glShaderSource(shader, 1, &sourceCStr, nullptr);

            glCompileShader(shader);

            GLint isCompiled = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
            if (isCompiled == GL_FALSE)
            {
                GLint maxLength = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

                std::vector<GLchar> infoLog(maxLength);
                glGetShaderInfoLog(shader, maxLength, &maxLength, infoLog.data());

                // 清理当前失败的 shader
                glDeleteShader(shader);
                // 清理之前已 attach 的 shader
                for (int i = 0; i < glShaderIDIndex; i++)
                {
                    glDetachShader(program, glShaderIDs[i]);
                    glDeleteShader(glShaderIDs[i]);
                }
                // 清理 program
                glDeleteProgram(program);

                ENGINE_CORE_ERROR("{0}", infoLog.data());
                ENGINE_CORE_RELEASE_ASSERT(false, "Shader compilation failure!");
                return;
            }

            glAttachShader(program, shader);
            glShaderIDs[glShaderIDIndex++] = shader;
        }

        glLinkProgram(program);

        GLint isLinked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
        if (isLinked == GL_FALSE)
        {
            GLint maxLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

            std::vector<GLchar> infoLog(maxLength);
            glGetProgramInfoLog(program, maxLength, &maxLength, infoLog.data());

            glDeleteProgram(program);
            for (int i = 0; i < glShaderIDIndex; i++)
                glDeleteShader(glShaderIDs[i]);

            ENGINE_CORE_ERROR("{0}", infoLog.data());
            ENGINE_CORE_RELEASE_ASSERT(false, "Shader link failure!");
            return;
        }

        for (int i = 0; i < glShaderIDIndex; i++)
        {
            glDetachShader(program, glShaderIDs[i]);
            glDeleteShader(glShaderIDs[i]);
        }

        m_RendererID = program;
    }

    void OpenGLShader::Bind() const
    {
        glUseProgram(m_RendererID);
    }

    void OpenGLShader::Unbind() const
    {
        glUseProgram(0);
    }

    void OpenGLShader::SetInt(const std::string& name, int value)
    {
        UploadUniformInt(name, value);
    }

    void OpenGLShader::SetIntArray(const std::string& name, int* values, uint32_t count)
    {
        UploadUniformIntArray(name, values, count);
    }

    void OpenGLShader::SetFloat(const std::string& name, float value)
    {
        UploadUniformFloat(name, value);
    }

    void OpenGLShader::SetFloat2(const std::string& name, const glm::vec2& value)
    {
        UploadUniformFloat2(name, value);
    }

    void OpenGLShader::SetFloat3(const std::string& name, const glm::vec3& value)
    {
        UploadUniformFloat3(name, value);
    }

    void OpenGLShader::SetFloat4(const std::string& name, const glm::vec4& value)
    {
        UploadUniformFloat4(name, value);
    }

    void OpenGLShader::SetMat3(const std::string& name, const glm::mat3& value)
    {
        UploadUniformMat3(name, value);
    }

    void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& value)
    {
        UploadUniformMat4(name, value);
    }

    void OpenGLShader::UploadUniformInt(const std::string& name, int value)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
            glUniform1i(location, value);
    }

    void OpenGLShader::UploadUniformIntArray(const std::string& name, int* values, uint32_t count)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
            glUniform1iv(location, count, values);
    }

    void OpenGLShader::UploadUniformFloat(const std::string& name, float value)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
            glUniform1f(location, value);
    }

    void OpenGLShader::UploadUniformFloat2(const std::string& name, const glm::vec2& value)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
            glUniform2f(location, value.x, value.y);
    }

    void OpenGLShader::UploadUniformFloat3(const std::string& name, const glm::vec3& value)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
            glUniform3f(location, value.x, value.y, value.z);
    }

    void OpenGLShader::UploadUniformFloat4(const std::string& name, const glm::vec4& value)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
            glUniform4f(location, value.x, value.y, value.z, value.w);
    }

    void OpenGLShader::UploadUniformMat3(const std::string& name, const glm::mat3& matrix)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
            glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }

    void OpenGLShader::UploadUniformMat4(const std::string& name, const glm::mat4& matrix)
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
            glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }

    GLint OpenGLShader::GetUniformLocation(const std::string& name) const
    {
        auto it = m_UniformLocationCache.find(name);
        if (it != m_UniformLocationCache.end())
            return it->second;

        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        if (location == -1)
            ENGINE_CORE_WARN("Uniform '{0}' not found in shader '{1}'", name, m_Name);
        m_UniformLocationCache[name] = location;
        return location;
    }

} // namespace Engine
