#pragma once

#include "imgui_render_core.h"
#include <array>
#include <cstdint>

#if defined(IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
#include <GL/gl3w.h>
#endif

namespace ImGuiRenderCore
{
struct GlAdapterStats
{
    uint32_t programBinds = 0;
    uint32_t textureBinds = 0;
    uint32_t blendStateChanges = 0;
    uint32_t drawCalls = 0;
};

class OpenGLAdapter
{
public:
    void BeginFrame();
    void ResetCache();

    void BindProgram(GLuint program);
    void ApplyBlendState(const BlendStateDesc& blendState);
    void BindTexture2D(uint32_t slot, GLuint textureId);
    void DrawElements(GLsizei elemCount, GLenum indexType, const void* indexOffset, GLint baseVertex, bool useBaseVertex);

    const GlAdapterStats& GetStats() const { return m_stats; }

private:
    static GLenum ToGLBlendFactor(BlendFactor factor);
    static GLenum ToGLBlendOp(BlendOp op);

    GLuint m_currentProgram = 0;
    std::array<GLuint, 16> m_boundTextures{};
    bool m_blendEnabled = false;
    GLenum m_srcColor = GL_ONE;
    GLenum m_dstColor = GL_ZERO;
    GLenum m_srcAlpha = GL_ONE;
    GLenum m_dstAlpha = GL_ZERO;
    GLenum m_colorOp = GL_FUNC_ADD;
    GLenum m_alphaOp = GL_FUNC_ADD;
    GlAdapterStats m_stats{};
};
} // namespace ImGuiRenderCore
