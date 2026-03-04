// Extension for imgui_impl_opengl3: SDF lines and Glow shader

#include "imgui_impl_opengl3_ext.h"
#include "imgui.h"

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdint.h>
#include <cstdlib>

#if !defined(IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
#include "imgui_impl_opengl3_loader.h"
#endif

#ifndef GL_ALPHA
#define GL_ALPHA 0x1906
#endif

struct ImGui_ImplOpenGL3_Ext_Data
{
    GLuint  SdfTextureHandle;
    GLuint  GlowTextureHandle;
    GLuint  SdfShaderHandle;
    GLuint  GlowShaderHandle;
    GLint   SdfAttribLocationTex;
    GLint   SdfAttribLocationProjMtx;
    GLint   GlowAttribLocationTex;
    GLint   GlowAttribLocationProjMtx;
    bool    Initialized;
};

static ImGui_ImplOpenGL3_Ext_Data g_ExtData = {};

static bool CheckShader(GLuint handle, const char* desc)
{
    GLint status = 0, log_length = 0;
    glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
    glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_length);
    if ((GLboolean)status == GL_FALSE)
        fprintf(stderr, "ImGui_ImplOpenGL3_Ext: failed to compile %s\n", desc);
    if (log_length > 1)
    {
        char* buf = (char*)malloc(log_length + 1);
        glGetShaderInfoLog(handle, log_length, nullptr, buf);
        fprintf(stderr, "%s\n", buf);
        free(buf);
    }
    return (GLboolean)status == GL_TRUE;
}

static bool CheckProgram(GLuint handle, const char* desc)
{
    GLint status = 0, log_length = 0;
    glGetProgramiv(handle, GL_LINK_STATUS, &status);
    glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &log_length);
    if ((GLboolean)status == GL_FALSE)
        fprintf(stderr, "ImGui_ImplOpenGL3_Ext: failed to link %s\n", desc);
    if (log_length > 1)
    {
        char* buf = (char*)malloc(log_length + 1);
        glGetProgramInfoLog(handle, log_length, nullptr, buf);
        fprintf(stderr, "%s\n", buf);
        free(buf);
    }
    return (GLboolean)status == GL_TRUE;
}

static void CreateSdfTexture(GLuint* out_tex)
{
    const int W = 64;
    unsigned char data[64];
    for (int i = 0; i < W; i++)
    {
        float x = (float)i / (float)(W - 1);
        float v = 4.0f * x * (1.0f - x);
        data[i] = (unsigned char)(v * 255.0f);
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#if defined(GL_UNPACK_ROW_LENGTH)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, W, 1, 0, GL_ALPHA, GL_UNSIGNED_BYTE, data);

    *out_tex = tex;
}

static const char* g_VertexShaderSource =
    "uniform mat4 ProjMtx;\n"
    "in vec2 Position;\n"
    "in vec2 UV;\n"
    "in vec4 Color;\n"
    "out vec2 Frag_UV;\n"
    "out vec4 Frag_Color;\n"
    "void main()\n"
    "{\n"
    "    Frag_UV = UV;\n"
    "    Frag_Color = Color;\n"
    "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
    "}\n";

static const char* g_SdfFragmentShaderSource =
    "uniform sampler2D Texture;\n"
    "in vec2 Frag_UV;\n"
    "in vec4 Frag_Color;\n"
    "out vec4 Out_Color;\n"
    "void main()\n"
    "{\n"
    "    float u = (abs(dFdx(Frag_UV.x)) > abs(dFdx(Frag_UV.y))) ? Frag_UV.x : Frag_UV.y;\n"
    "    u = clamp((u - 0.5) * 2.0 + 0.5, 0.0, 1.0);\n"
    "    float d = texture(Texture, vec2(u, 0.5)).r;\n"
    "    float alpha = smoothstep(0.25, 0.75, d);\n"
    "    Out_Color = Frag_Color * alpha;\n"
    "}\n";

static const char* g_GlowFragmentShaderSource =
    "uniform sampler2D Texture;\n"
    "in vec2 Frag_UV;\n"
    "in vec4 Frag_Color;\n"
    "out vec4 Out_Color;\n"
    "void main()\n"
    "{\n"
    "    float u = (abs(dFdx(Frag_UV.x)) > abs(dFdx(Frag_UV.y))) ? Frag_UV.x : Frag_UV.y;\n"
    "    u = clamp((u - 0.5) * 2.0 + 0.5, 0.0, 1.0);\n"
    "    float d = texture(Texture, vec2(u, 0.5)).r;\n"
    "    float alpha = smoothstep(0.2, 0.8, d);\n"
    "    Out_Color = Frag_Color * alpha;\n"
    "}\n";

#if defined(IMGUI_IMPL_OPENGL_ES2)
static const char* g_VertexShaderSource_ES2 =
    "attribute vec2 Position;\n"
    "attribute vec2 UV;\n"
    "attribute vec4 Color;\n"
    "uniform mat4 ProjMtx;\n"
    "varying vec2 Frag_UV;\n"
    "varying vec4 Frag_Color;\n"
    "void main()\n"
    "{\n"
    "    Frag_UV = UV;\n"
    "    Frag_Color = Color;\n"
    "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
    "}\n";

static const char* g_SdfFragmentShaderSource_ES2 =
    "#ifdef GL_ES\n"
    "    precision mediump float;\n"
    "    #extension GL_OES_standard_derivatives : require\n"
    "#endif\n"
    "uniform sampler2D Texture;\n"
    "varying vec2 Frag_UV;\n"
    "varying vec4 Frag_Color;\n"
    "void main()\n"
    "{\n"
    "    float u = (abs(dFdx(Frag_UV.x)) > abs(dFdx(Frag_UV.y))) ? Frag_UV.x : Frag_UV.y;\n"
    "    u = clamp((u - 0.5) * 2.0 + 0.5, 0.0, 1.0);\n"
    "    float d = texture2D(Texture, vec2(u, 0.5)).r;\n"
    "    float alpha = smoothstep(0.25, 0.75, d);\n"
    "    gl_FragColor = Frag_Color * alpha;\n"
    "}\n";

static const char* g_GlowFragmentShaderSource_ES2 =
    "#ifdef GL_ES\n"
    "    precision mediump float;\n"
    "    #extension GL_OES_standard_derivatives : require\n"
    "#endif\n"
    "uniform sampler2D Texture;\n"
    "varying vec2 Frag_UV;\n"
    "varying vec4 Frag_Color;\n"
    "void main()\n"
    "{\n"
    "    float u = (abs(dFdx(Frag_UV.x)) > abs(dFdx(Frag_UV.y))) ? Frag_UV.x : Frag_UV.y;\n"
    "    u = clamp((u - 0.5) * 2.0 + 0.5, 0.0, 1.0);\n"
    "    float d = texture2D(Texture, vec2(u, 0.5)).r;\n"
    "    float alpha = smoothstep(0.2, 0.8, d);\n"
    "    gl_FragColor = Frag_Color * alpha;\n"
    "}\n";
#endif

static bool CompileShader(const char* glsl_version, bool use_glow_fs, GLuint* out_program)
{
#if defined(IMGUI_IMPL_OPENGL_ES2)
    const char* vs = g_VertexShaderSource_ES2;
    const char* fs = use_glow_fs ? g_GlowFragmentShaderSource_ES2 : g_SdfFragmentShaderSource_ES2;
#else
    const char* vs = g_VertexShaderSource;
    const char* fs = use_glow_fs ? g_GlowFragmentShaderSource : g_SdfFragmentShaderSource;
#endif

    const char* version_str = (glsl_version && glsl_version[0]) ? glsl_version : "#version 130\n";
    const char* vs_sources[2] = { version_str, vs };
    const char* fs_sources[2] = { version_str, fs };

    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 2, vs_sources, nullptr);
    glCompileShader(vert);
    if (!CheckShader(vert, "SDF vertex"))
    {
        glDeleteShader(vert);
        return false;
    }

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 2, fs_sources, nullptr);
    glCompileShader(frag);
    if (!CheckShader(frag, "SDF fragment"))
    {
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    glDetachShader(prog, vert);
    glDetachShader(prog, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);

    if (!CheckProgram(prog, "SDF program"))
    {
        glDeleteProgram(prog);
        return false;
    }

    *out_program = prog;
    return true;
}

bool ImGui_ImplOpenGL3_Ext_Init(const char* glsl_version)
{
    if (g_ExtData.Initialized)
        return true;

    const char* ver = (glsl_version && glsl_version[0]) ? glsl_version : "#version 130";

    GLint last_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);

    CreateSdfTexture(&g_ExtData.SdfTextureHandle);
    CreateSdfTexture(&g_ExtData.GlowTextureHandle);

    glBindTexture(GL_TEXTURE_2D, last_texture);

    if (!CompileShader(ver, false, &g_ExtData.SdfShaderHandle))
        return false;
    if (!CompileShader(ver, true, &g_ExtData.GlowShaderHandle))
        return false;

    g_ExtData.SdfAttribLocationTex = glGetUniformLocation(g_ExtData.SdfShaderHandle, "Texture");
    g_ExtData.SdfAttribLocationProjMtx = glGetUniformLocation(g_ExtData.SdfShaderHandle, "ProjMtx");
    g_ExtData.GlowAttribLocationTex = glGetUniformLocation(g_ExtData.GlowShaderHandle, "Texture");
    g_ExtData.GlowAttribLocationProjMtx = glGetUniformLocation(g_ExtData.GlowShaderHandle, "ProjMtx");

    g_ExtData.Initialized = true;
    return true;
}

void ImGui_ImplOpenGL3_Ext_Shutdown(void)
{
    if (!g_ExtData.Initialized)
        return;

    if (g_ExtData.SdfTextureHandle)
    {
        glDeleteTextures(1, &g_ExtData.SdfTextureHandle);
        g_ExtData.SdfTextureHandle = 0;
    }
    if (g_ExtData.GlowTextureHandle)
    {
        glDeleteTextures(1, &g_ExtData.GlowTextureHandle);
        g_ExtData.GlowTextureHandle = 0;
    }
    if (g_ExtData.SdfShaderHandle)
    {
        glDeleteProgram(g_ExtData.SdfShaderHandle);
        g_ExtData.SdfShaderHandle = 0;
    }
    if (g_ExtData.GlowShaderHandle)
    {
        glDeleteProgram(g_ExtData.GlowShaderHandle);
        g_ExtData.GlowShaderHandle = 0;
    }

    g_ExtData.Initialized = false;
}

ImTextureID ImGui_ImplOpenGL3_Ext_GetSdfTextureId(void)
{
    if (!g_ExtData.Initialized)
        return (ImTextureID)(intptr_t)0;
    return (ImTextureID)(intptr_t)(uintptr_t)g_ExtData.SdfTextureHandle;
}

ImTextureID ImGui_ImplOpenGL3_Ext_GetGlowTextureId(void)
{
    if (!g_ExtData.Initialized)
        return (ImTextureID)(intptr_t)0;
    return (ImTextureID)(intptr_t)(uintptr_t)g_ExtData.GlowTextureHandle;
}

IMGUI_IMPL_API bool ImGui_ImplOpenGL3_Ext_IsSdfTexture(ImTextureID tex_id)
{
    if (!g_ExtData.Initialized)
        return false;
    return (ImTextureID)(intptr_t)(uintptr_t)g_ExtData.SdfTextureHandle == tex_id;
}

IMGUI_IMPL_API bool ImGui_ImplOpenGL3_Ext_IsGlowTexture(ImTextureID tex_id)
{
    if (!g_ExtData.Initialized)
        return false;
    return (ImTextureID)(intptr_t)(uintptr_t)g_ExtData.GlowTextureHandle == tex_id;
}

IMGUI_IMPL_API void ImGui_ImplOpenGL3_Ext_ApplySdfState(ImDrawData* draw_data, int fb_width, int fb_height, unsigned int attrib_pos, unsigned int attrib_uv, unsigned int attrib_col)
{
    (void)attrib_pos;
    (void)attrib_uv;
    (void)attrib_col;
    if (!g_ExtData.Initialized)
        return;

    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    const float ortho[4][4] = {
        { 2.0f/(R-L), 0, 0, 0 },
        { 0, 2.0f/(T-B), 0, 0 },
        { 0, 0, -1, 0 },
        { (R+L)/(L-R), (T+B)/(B-T), 0, 1 },
    };

    glUseProgram(g_ExtData.SdfShaderHandle);
    glUniform1i(g_ExtData.SdfAttribLocationTex, 0);
    glUniformMatrix4fv(g_ExtData.SdfAttribLocationProjMtx, 1, GL_FALSE, &ortho[0][0]);
    glBindTexture(GL_TEXTURE_2D, g_ExtData.SdfTextureHandle);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

IMGUI_IMPL_API void ImGui_ImplOpenGL3_Ext_ApplyGlowState(ImDrawData* draw_data, int fb_width, int fb_height, unsigned int attrib_pos, unsigned int attrib_uv, unsigned int attrib_col)
{
    (void)fb_width;
    (void)fb_height;
    (void)attrib_pos;
    (void)attrib_uv;
    (void)attrib_col;
    if (!g_ExtData.Initialized)
        return;

    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    const float ortho[4][4] = {
        { 2.0f/(R-L), 0, 0, 0 },
        { 0, 2.0f/(T-B), 0, 0 },
        { 0, 0, -1, 0 },
        { (R+L)/(L-R), (T+B)/(B-T), 0, 1 },
    };

    glUseProgram(g_ExtData.GlowShaderHandle);
    glUniform1i(g_ExtData.GlowAttribLocationTex, 0);
    glUniformMatrix4fv(g_ExtData.GlowAttribLocationProjMtx, 1, GL_FALSE, &ortho[0][0]);
    glBindTexture(GL_TEXTURE_2D, g_ExtData.GlowTextureHandle);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
}
