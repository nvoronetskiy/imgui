// Ensure GL types are available when this TU is built (e.g. by vcpkg) without
// IMGUI_IMPL_OPENGL_LOADER_CUSTOM from the build system.
#ifndef IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif
#include "imgui_gl_adapter.h"

namespace ImGuiRenderCore
{
GLenum OpenGLAdapter::ToGLBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::Zero: return GL_ZERO;
    case BlendFactor::One: return GL_ONE;
    case BlendFactor::SrcColor: return GL_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DstColor: return GL_DST_COLOR;
    case BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
    case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha: return GL_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    default: return GL_ONE;
    }
}

GLenum OpenGLAdapter::ToGLBlendOp(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add: return GL_FUNC_ADD;
    case BlendOp::Subtract: return GL_FUNC_SUBTRACT;
    case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
    default: return GL_FUNC_ADD;
    }
}

void OpenGLAdapter::BeginFrame()
{
    m_stats = {};
    // External code restores GL state after our pass, so cached bindings
    // may become stale across frames. Reset cache each frame.
    ResetCache();
}

void OpenGLAdapter::ResetCache()
{
    m_currentProgram = 0;
    m_boundTextures.fill(0);
    m_blendEnabled = false;
    m_srcColor = GL_ONE;
    m_dstColor = GL_ZERO;
    m_srcAlpha = GL_ONE;
    m_dstAlpha = GL_ZERO;
    m_colorOp = GL_FUNC_ADD;
    m_alphaOp = GL_FUNC_ADD;
}

void OpenGLAdapter::BindProgram(GLuint program)
{
    if (m_currentProgram == program)
        return;
    glUseProgram(program);
    m_currentProgram = program;
    m_stats.programBinds++;
}

void OpenGLAdapter::ApplyBlendState(const BlendStateDesc& blendState)
{
    const GLenum srcColor = ToGLBlendFactor(blendState.srcColor);
    const GLenum dstColor = ToGLBlendFactor(blendState.dstColor);
    const GLenum srcAlpha = ToGLBlendFactor(blendState.srcAlpha);
    const GLenum dstAlpha = ToGLBlendFactor(blendState.dstAlpha);
    const GLenum colorOp = ToGLBlendOp(blendState.colorOp);
    const GLenum alphaOp = ToGLBlendOp(blendState.alphaOp);

    if (m_blendEnabled != blendState.enabled)
    {
        if (blendState.enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        m_blendEnabled = blendState.enabled;
        m_stats.blendStateChanges++;
    }
    if (!blendState.enabled)
        return;

    if (m_srcColor != srcColor || m_dstColor != dstColor || m_srcAlpha != srcAlpha || m_dstAlpha != dstAlpha)
    {
        glBlendFuncSeparate(srcColor, dstColor, srcAlpha, dstAlpha);
        m_srcColor = srcColor;
        m_dstColor = dstColor;
        m_srcAlpha = srcAlpha;
        m_dstAlpha = dstAlpha;
        m_stats.blendStateChanges++;
    }
    if (m_colorOp != colorOp || m_alphaOp != alphaOp)
    {
        glBlendEquationSeparate(colorOp, alphaOp);
        m_colorOp = colorOp;
        m_alphaOp = alphaOp;
        m_stats.blendStateChanges++;
    }
}

void OpenGLAdapter::BindTexture2D(uint32_t slot, GLuint textureId)
{
    if (slot >= m_boundTextures.size())
        return;
    if (m_boundTextures[slot] == textureId)
        return;
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, textureId);
    m_boundTextures[slot] = textureId;
    m_stats.textureBinds++;
}

void OpenGLAdapter::DrawElements(GLsizei elemCount, GLenum indexType, const void* indexOffset, GLint baseVertex, bool useBaseVertex)
{
    if (useBaseVertex)
        glDrawElementsBaseVertex(GL_TRIANGLES, elemCount, indexType, indexOffset, baseVertex);
    else
        glDrawElements(GL_TRIANGLES, elemCount, indexType, indexOffset);
    m_stats.drawCalls++;
}
} // namespace ImGuiRenderCore
