#include "engpch.h"
#include "Platform/OpenGL/OpenGLRendererAPI.h"

#include <glad/gl.h>

#include "Core/Log.h"

namespace Engine
{

    void OpenGLRendererAPI::Init()
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LINE_SMOOTH);
    }

    void OpenGLRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        glViewport(x, y, width, height);
    }

    void OpenGLRendererAPI::SetClearColor(const glm::vec4& color)
    {
        glClearColor(color.r, color.g, color.b, color.a);
    }

    void OpenGLRendererAPI::Clear()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
    {
        vertexArray->Bind();
        uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    }

    void OpenGLRendererAPI::DrawArrays(uint32_t count, uint32_t first)
    {
        glDrawArrays(GL_TRIANGLES, first, count);
    }

    void OpenGLRendererAPI::DrawLines(uint32_t count, uint32_t first)
    {
        glDrawArrays(GL_LINES, first, count);
    }

    void OpenGLRendererAPI::SetDepthTest(bool enable)
    {
        if (enable)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
    }

    void OpenGLRendererAPI::SetDepthFunc(DepthFunc func)
    {
        GLenum glFunc = GL_LESS;
        switch (func)
        {
        case DepthFunc::Less:    glFunc = GL_LESS;    break;
        case DepthFunc::LEqual:  glFunc = GL_LEQUAL;  break;
        case DepthFunc::Greater: glFunc = GL_GREATER;  break;
        case DepthFunc::Always:  glFunc = GL_ALWAYS;   break;
        }
        glDepthFunc(glFunc);
    }

    void OpenGLRendererAPI::SetCullFace(bool enable)
    {
        if (enable)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);
    }

    void OpenGLRendererAPI::SetCullFaceMode(CullFaceMode mode)
    {
        glCullFace(mode == CullFaceMode::Front ? GL_FRONT : GL_BACK);
    }

    void OpenGLRendererAPI::SetLineWidth(float width)
    {
        glLineWidth(width);
    }

    void OpenGLRendererAPI::BindTextureUnit(uint32_t slot, uint32_t textureID)
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, textureID);
    }

    void OpenGLRendererAPI::ClearColorOnly()
    {
        glClear(GL_COLOR_BUFFER_BIT);
    }

    int OpenGLRendererAPI::GetBoundFramebufferID()
    {
        GLint id = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &id);
        return static_cast<int>(id);
    }

    void OpenGLRendererAPI::BindFramebufferByID(int id)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(id));
    }

} // namespace Engine
