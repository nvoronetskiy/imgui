// dear imgui: Renderer Backend for OpenGL3 with Slang shader compiler
// Based on imgui_impl_opengl3.cpp - uses Slang to compile shaders instead of embedded GLSL.
// - Desktop GL: 3.3+ (GLSL 330)
// Requires: slang (shader-slang) library, gl3w (via IMGUI_IMPL_OPENGL_LOADER_CUSTOM)

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

// This backend uses gl3w - ensure custom loader is used (imgui_gl_config.h)
#ifndef IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_opengl3_slang.h"
#include "imgui_gl_adapter.h"
#include "imgui_shader_manager_slang.h"
#include <stdio.h>
#include <stdint.h>     // intptr_t
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// Clang/GCC warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option" // warning: ignore unknown flags
#pragma clang diagnostic ignored "-Wold-style-cast"         // warning: use of old-style cast
#pragma clang diagnostic ignored "-Wsign-conversion"        // warning: implicit conversion changes signedness
#pragma clang diagnostic ignored "-Wunused-macros"          // warning: macro is not used
#pragma clang diagnostic ignored "-Wnonportable-system-include-path"
#pragma clang diagnostic ignored "-Wcast-function-type"     // warning: cast between incompatible function types (for loader)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"                  // warning: unknown option after '#pragma GCC diagnostic' kind
#pragma GCC diagnostic ignored "-Wunknown-warning-option"   // warning: unknown warning group 'xxx'
#pragma GCC diagnostic ignored "-Wcast-function-type"       // warning: cast between incompatible function types (for loader)
#pragma GCC diagnostic ignored "-Wstrict-overflow"          // warning: assuming signed overflow does not occur when simplifying division / ..when changing X +- C1 cmp C2 to X cmp C2 -+ C1
#endif

// GL includes - IMGUI_IMPL_OPENGL_LOADER_CUSTOM: assumes gl3w from imgui_gl_config.h
#if defined(IMGUI_IMPL_OPENGL_ES2)
#if (defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV))
#include <OpenGLES/ES2/gl.h>    // Use GL ES 2
#else
#include <GLES2/gl2.h>          // Use GL ES 2
#endif
#if defined(__EMSCRIPTEN__)
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GLES2/gl2ext.h>
#endif
#elif defined(IMGUI_IMPL_OPENGL_ES3)
#if (defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV))
#include <OpenGLES/ES3/gl.h>    // Use GL ES 3
#else
#include <GLES3/gl3.h>          // Use GL ES 3
#endif
#elif defined(IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
// gl3w is provided by imgui_gl_config.h / IMGUI_USER_CONFIG
#include <GL/gl3w.h>
#else
#define IMGL3W_IMPL
#define IMGUI_IMPL_OPENGL_LOADER_IMGL3W
#include "imgui_impl_opengl3_loader.h"
#endif

// Vertex arrays are not supported on ES2/WebGL1 unless Emscripten which uses an extension
#ifndef IMGUI_IMPL_OPENGL_ES2
#define IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
#elif defined(__EMSCRIPTEN__)
#define IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
#define glBindVertexArray       glBindVertexArrayOES
#define glGenVertexArrays       glGenVertexArraysOES
#define glDeleteVertexArrays    glDeleteVertexArraysOES
#define GL_VERTEX_ARRAY_BINDING GL_VERTEX_ARRAY_BINDING_OES
#endif

// Desktop GL 2.0+ has extension and glPolygonMode() which GL ES and WebGL don't have..
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(IMGUI_IMPL_OPENGL_ES3)
#define IMGUI_IMPL_OPENGL_HAS_EXTENSIONS        // has glGetIntegerv(GL_NUM_EXTENSIONS)
#define IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE // may have glPolygonMode()
#endif

// Desktop GL 2.1+ and GL ES 3.0+ have glBindBuffer() with GL_PIXEL_UNPACK_BUFFER target.
#if !defined(IMGUI_IMPL_OPENGL_ES2)
#define IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_BUFFER_PIXEL_UNPACK
#endif

// Desktop GL 3.1+ has GL_PRIMITIVE_RESTART state
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(IMGUI_IMPL_OPENGL_ES3) && defined(GL_VERSION_3_1)
#define IMGUI_IMPL_OPENGL_MAY_HAVE_PRIMITIVE_RESTART
#endif

// Desktop GL 3.2+ has glDrawElementsBaseVertex() which GL ES and WebGL don't have.
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(IMGUI_IMPL_OPENGL_ES3) && defined(GL_VERSION_3_2)
#define IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET
#endif

// Desktop GL 3.3+ and GL ES 3.0+ have glBindSampler()
#if !defined(IMGUI_IMPL_OPENGL_ES2) && (defined(IMGUI_IMPL_OPENGL_ES3) || defined(GL_VERSION_3_3))
#define IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_SAMPLER
#endif

// [Debugging]
//#define IMGUI_IMPL_OPENGL_DEBUG
#ifdef IMGUI_IMPL_OPENGL_DEBUG
#include <stdio.h>
#define GL_CALL(_CALL)      do { _CALL; GLenum gl_err = glGetError(); if (gl_err != 0) fprintf(stderr, "GL error 0x%x returned from '%s'.\n", gl_err, #_CALL); } while (0)  // Call with error check
#else
#define GL_CALL(_CALL)      _CALL   // Call without error check
#endif

// OpenGL Data
struct ImGui_ImplOpenGL3Slang_Data
{
    GLuint          GlVersion;               // Extracted at runtime using GL_MAJOR_VERSION, GL_MINOR_VERSION queries (e.g. 320 for GL 3.2)
    char            GlslVersionString[32];   // Specified by user or detected based on compile time GL settings.
    bool            GlProfileIsES2;
    bool            GlProfileIsES3;
    bool            GlProfileIsCompat;
    GLint           GlProfileMask;
    GLint           MaxTextureSize;
    GLuint          ShaderHandle;
    GLint           AttribLocationTex;       // Texture uniform (Slang: Texture_0)
    GLuint          UboHandle;               // UBO for ProjMtx (Slang uses uniform block)
    GLuint          AttribLocationVtxPos;    // Vertex attributes (Slang: Position_0, UV_0, Color_0)
    GLuint          AttribLocationVtxUV;
    GLuint          AttribLocationVtxColor;
    unsigned int    VboHandle, ElementsHandle;
    GLuint          EffectParamsUbo;         // std140 scratch buffer for per-draw effect uniforms (binding 2)
    GLsizeiptr      VertexBufferSize;
    GLsizeiptr      IndexBufferSize;
    bool            HasPolygonMode;
    bool            HasBindSampler;
    bool            HasClipOrigin;
    bool            UseBufferSubData;
    ImVector<char>  TempBuffer;
    ImGuiRenderCore::ShaderManagerSlang* ShaderManager;
    ImGuiRenderCore::OpenGLAdapter* GlAdapter;
    ImGuiRenderCore::FrameCommandList* FrameCommands;
    ImGuiRenderCore::CustomPassRegistry* CustomPasses;

    ImGui_ImplOpenGL3Slang_Data() { memset((void*)this, 0, sizeof(*this)); }
};

static std::unordered_map<std::string, ImGuiRenderCore::BlendStateDesc> g_BlendModes;
static std::unordered_map<std::string, ImGuiRenderCore::PipelineDesc> g_Pipelines;
static std::unordered_map<std::string, ImGuiRenderCore::ShaderDesc> g_Shaders;
struct ShaderProgramState
{
    GLuint program = 0;
    GLint textureLocation = -1;
};
static std::unordered_map<std::string, ShaderProgramState> g_ShaderPrograms;
static ImGui_ImplOpenGL3Slang_Stats g_LastStats;
static std::string g_LastErrorText;

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
static ImGui_ImplOpenGL3Slang_Data* ImGui_ImplOpenGL3Slang_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplOpenGL3Slang_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

// Forward Declarations
static void ImGui_ImplOpenGL3Slang_InitMultiViewportSupport();
static void ImGui_ImplOpenGL3Slang_ShutdownMultiViewportSupport();

// OpenGL vertex attribute state (for ES 1.0 and ES 2.0 only)
#ifndef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
struct ImGui_ImplOpenGL3Slang_VtxAttribState
{
    GLint   Enabled, Size, Type, Normalized, Stride;
    GLvoid* Ptr;

    void GetState(GLint index)
    {
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &Enabled);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &Size);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &Type);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &Normalized);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &Stride);
        glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &Ptr);
    }
    void SetState(GLint index)
    {
        glVertexAttribPointer(index, Size, Type, (GLboolean)Normalized, Stride, Ptr);
        if (Enabled) glEnableVertexAttribArray(index); else glDisableVertexAttribArray(index);
    }
};
#endif

// Not static to allow third-party code to use that if they want to (but undocumented)
bool ImGui_ImplOpenGL3Slang_InitLoader();
bool ImGui_ImplOpenGL3Slang_InitLoader()
{
#ifdef IMGUI_IMPL_OPENGL_LOADER_CUSTOM
    // gl3w: load OpenGL function pointers. Call after the GL context is current.
    if (gl3wInit() != 0)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader (gl3w). Ensure the GL context is current before ImGui_ImplOpenGL3Slang_Init().\n");
        return false;
    }
#elif defined(IMGUI_IMPL_OPENGL_LOADER_IMGL3W)
    if (glGetIntegerv == nullptr && imgl3wInit() != 0)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return false;
    }
#endif
    return true;
}

static void ImGui_ImplOpenGL3Slang_ShutdownLoader()
{
#ifdef IMGUI_IMPL_OPENGL_LOADER_IMGL3W
    imgl3wShutdown();
#endif
}

// Functions
bool    ImGui_ImplOpenGL3Slang_Init(const char* glsl_version)
{
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    // Initialize loader
    if (!ImGui_ImplOpenGL3Slang_InitLoader())
        return false;

    // Setup backend capabilities flags
    ImGui_ImplOpenGL3Slang_Data* bd = IM_NEW(ImGui_ImplOpenGL3Slang_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_opengl3_slang";
    bd->ShaderManager = new ImGuiRenderCore::ShaderManagerSlang();
    bd->GlAdapter = new ImGuiRenderCore::OpenGLAdapter();
    bd->FrameCommands = new ImGuiRenderCore::FrameCommandList();
    bd->CustomPasses = new ImGuiRenderCore::CustomPassRegistry();

    // Query for GL version (e.g. 320 for GL 3.2)
    const char* gl_version_str = (const char*)glGetString(GL_VERSION);
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GLES 2
    bd->GlVersion = 200;
    bd->GlProfileIsES2 = true;
    IM_UNUSED(gl_version_str);
#else
    // Desktop or GLES 3
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major == 0 && minor == 0)
        sscanf(gl_version_str, "%d.%d", &major, &minor); // Query GL_VERSION in desktop GL 2.x, the string will start with "<major>.<minor>"
    bd->GlVersion = (GLuint)(major * 100 + minor * 10);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &bd->MaxTextureSize);

#if defined(IMGUI_IMPL_OPENGL_ES3)
    bd->GlProfileIsES3 = true;
#else
    if (strncmp(gl_version_str, "OpenGL ES 3", 11) == 0)
        bd->GlProfileIsES3 = true;
#endif

#if defined(GL_CONTEXT_PROFILE_MASK)
    if (!bd->GlProfileIsES3 && bd->GlVersion >= 320)
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &bd->GlProfileMask);
    bd->GlProfileIsCompat = (bd->GlProfileMask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) != 0;
#endif

    bd->UseBufferSubData = false;
#endif

#ifdef IMGUI_IMPL_OPENGL_DEBUG
    printf("GlVersion = %d, \"%s\"\nGlProfileIsCompat = %d\nGlProfileMask = 0x%X\nGlProfileIsES2/IsEs3 = %d/%d\nGL_VENDOR = '%s'\nGL_RENDERER = '%s'\n", bd->GlVersion, gl_version_str, bd->GlProfileIsCompat, bd->GlProfileMask, bd->GlProfileIsES2, bd->GlProfileIsES3, (const char*)glGetString(GL_VENDOR), (const char*)glGetString(GL_RENDERER)); // [DEBUG]
#endif

#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET
    if (bd->GlVersion >= 320)
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
#endif
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;       // We can honor ImGuiPlatformIO::Textures[] requests during render.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;      // We can create multi-viewports on the Renderer side (optional)

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight = (int)bd->MaxTextureSize;

    // Store GLSL version string so we can refer to it later in case we recreate shaders.
    if (glsl_version == nullptr)
    {
#if defined(IMGUI_IMPL_OPENGL_ES2)
        glsl_version = "#version 100";
#elif defined(IMGUI_IMPL_OPENGL_ES3)
        glsl_version = "#version 300 es";
#elif defined(__APPLE__)
        glsl_version = "#version 150";
#else
        glsl_version = "#version 330";
#endif
    }
    IM_ASSERT((int)strlen(glsl_version) + 2 < IM_COUNTOF(bd->GlslVersionString));
    strcpy(bd->GlslVersionString, glsl_version);
    strcat(bd->GlslVersionString, "\n");

    // Make an arbitrary GL call (we don't actually need the result)
    GLint current_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_texture);

    // Detect extensions we support
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE
    bd->HasPolygonMode = (!bd->GlProfileIsES2 && !bd->GlProfileIsES3);
#endif
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_SAMPLER
    bd->HasBindSampler = (bd->GlVersion >= 330 || bd->GlProfileIsES3);
#endif
    bd->HasClipOrigin = (bd->GlVersion >= 450);
#ifdef IMGUI_IMPL_OPENGL_HAS_EXTENSIONS
    GLint num_extensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
    for (GLint i = 0; i < num_extensions; i++)
    {
        const char* extension = (const char*)glGetStringi(GL_EXTENSIONS, i);
        if (extension != nullptr && strcmp(extension, "GL_ARB_clip_control") == 0)
            bd->HasClipOrigin = true;
    }
#endif

    ImGui_ImplOpenGL3Slang_InitMultiViewportSupport();

    if (g_BlendModes.empty())
    {
        ImGuiRenderCore::BlendStateDesc alphaBlend;
        g_BlendModes["alpha"] = alphaBlend;
    }
    if (g_Pipelines.empty())
    {
        ImGuiRenderCore::PipelineDesc defaultPipeline;
        defaultPipeline.pipelineKey = "imgui_default";
        defaultPipeline.shaderKey = "imgui_default";
        defaultPipeline.blendModeKey = "alpha";
        g_Pipelines[defaultPipeline.pipelineKey] = defaultPipeline;
    }

    return true;
}

void    ImGui_ImplOpenGL3Slang_Shutdown()
{
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    ImGui_ImplOpenGL3Slang_ShutdownMultiViewportSupport();
    ImGui_ImplOpenGL3Slang_DestroyDeviceObjects();
    delete bd->ShaderManager;
    delete bd->GlAdapter;
    delete bd->FrameCommands;
    delete bd->CustomPasses;

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasViewports);
    platform_io.ClearRendererHandlers();
    IM_DELETE(bd);

    ImGui_ImplOpenGL3Slang_ShutdownLoader();
}

void    ImGui_ImplOpenGL3Slang_NewFrame()
{
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplOpenGL3Slang_Init()?");

    ImGui_ImplOpenGL3Slang_InitLoader();
    if (bd->GlAdapter)
        bd->GlAdapter->BeginFrame();
    if (bd->CustomPasses)
        bd->CustomPasses->Clear(); // drop stale packets carrying draw-list pointers from previous frames
    if (!bd->ShaderHandle)
        if (!ImGui_ImplOpenGL3Slang_CreateDeviceObjects())
            IM_ASSERT(0 && "ImGui_ImplOpenGL3Slang_CreateDeviceObjects() failed!");
}

static void ImGui_ImplOpenGL3Slang_SetupRenderState(ImDrawData* draw_data, int fb_width, int fb_height, GLuint vertex_array_object)
{
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();

    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, polygon fill
    if (bd->GlAdapter)
    {
        auto itBlend = g_BlendModes.find("alpha");
        if (itBlend != g_BlendModes.end())
            bd->GlAdapter->ApplyBlendState(itBlend->second);
    }
    else
    {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_SCISSOR_TEST);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_PRIMITIVE_RESTART
    if (!bd->GlProfileIsES3 && bd->GlVersion >= 310)
        glDisable(GL_PRIMITIVE_RESTART);
#endif
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE
    if (bd->HasPolygonMode)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    // Support for GL 4.5 rarely used glClipControl(GL_UPPER_LEFT)
#if defined(GL_CLIP_ORIGIN)
    bool clip_origin_lower_left = true;
    if (bd->HasClipOrigin)
    {
        GLenum current_clip_origin = 0; glGetIntegerv(GL_CLIP_ORIGIN, (GLint*)&current_clip_origin);
        if (current_clip_origin == GL_UPPER_LEFT)
            clip_origin_lower_left = false;
    }
#endif

    // Setup viewport, orthographic projection matrix
    GL_CALL(glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height));
    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
#if defined(GL_CLIP_ORIGIN)
    if (!clip_origin_lower_left) { float tmp = T; T = B; B = tmp; } // Swap top and bottom if origin is upper left
#endif
    const float ortho_projection[4][4] =
    {
        { 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
        { 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
        { 0.0f,         0.0f,        -1.0f,   0.0f },
        { (R+L)/(L-R),  (T+B)/(B-T),  0.0f,   1.0f },
    };
    if (bd->GlAdapter) bd->GlAdapter->BindProgram(bd->ShaderHandle); else glUseProgram(bd->ShaderHandle);
    glActiveTexture(GL_TEXTURE0);  // ensure texture unit 0 is active before setting uniform
    glUniform1i(bd->AttribLocationTex, 0);
    GL_CALL(glBindBuffer(GL_UNIFORM_BUFFER, bd->UboHandle));
    GL_CALL(glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ortho_projection), &ortho_projection[0][0]));
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, bd->UboHandle);

#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_SAMPLER
    if (bd->HasBindSampler)
        glBindSampler(0, 0);
#endif

    (void)vertex_array_object;
#ifdef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
    glBindVertexArray(vertex_array_object);
#endif

    // Bind vertex/index buffers and setup attributes for ImDrawVert
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, bd->VboHandle));
    GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bd->ElementsHandle));
    GL_CALL(glEnableVertexAttribArray(bd->AttribLocationVtxPos));
    GL_CALL(glEnableVertexAttribArray(bd->AttribLocationVtxUV));
    GL_CALL(glEnableVertexAttribArray(bd->AttribLocationVtxColor));
    GL_CALL(glVertexAttribPointer(bd->AttribLocationVtxPos,   2, GL_FLOAT,         GL_FALSE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, pos)));
    GL_CALL(glVertexAttribPointer(bd->AttribLocationVtxUV,    2, GL_FLOAT,         GL_FALSE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, uv)));
    GL_CALL(glVertexAttribPointer(bd->AttribLocationVtxColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, col)));
}

void    ImGui_ImplOpenGL3Slang_RenderDrawData(ImDrawData* draw_data)
{
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0)
        return;

    ImGui_ImplOpenGL3Slang_InitLoader();

    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
    // Reset per-context cached GL state before each viewport render call.
    // Multi-viewport rendering can switch GL contexts between calls.
    if (bd->GlAdapter)
        bd->GlAdapter->ResetCache();

    // Catch up with texture updates
    if (draw_data->Textures != nullptr)
        for (ImTextureData* tex : *draw_data->Textures)
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplOpenGL3Slang_UpdateTexture(tex);

    // Backup GL state
    GLenum last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&last_active_texture);
    glActiveTexture(GL_TEXTURE0);
    GLuint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*)&last_program);
    GLuint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, (GLint*)&last_texture);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_SAMPLER
    GLuint last_sampler; if (bd->HasBindSampler) { glGetIntegerv(GL_SAMPLER_BINDING, (GLint*)&last_sampler); } else { last_sampler = 0; }
#endif
    GLuint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, (GLint*)&last_array_buffer);
#ifndef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
    GLint last_element_array_buffer; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &last_element_array_buffer);
    ImGui_ImplOpenGL3Slang_VtxAttribState last_vtx_attrib_state_pos; last_vtx_attrib_state_pos.GetState(bd->AttribLocationVtxPos);
    ImGui_ImplOpenGL3Slang_VtxAttribState last_vtx_attrib_state_uv; last_vtx_attrib_state_uv.GetState(bd->AttribLocationVtxUV);
    ImGui_ImplOpenGL3Slang_VtxAttribState last_vtx_attrib_state_color; last_vtx_attrib_state_color.GetState(bd->AttribLocationVtxColor);
#endif
#ifdef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
    GLuint last_vertex_array_object; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, (GLint*)&last_vertex_array_object);
#endif
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE
    GLint last_polygon_mode[2]; if (bd->HasPolygonMode) { glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode); }
#endif
    GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    GLenum last_blend_src_rgb; glGetIntegerv(GL_BLEND_SRC_RGB, (GLint*)&last_blend_src_rgb);
    GLenum last_blend_dst_rgb; glGetIntegerv(GL_BLEND_DST_RGB, (GLint*)&last_blend_dst_rgb);
    GLenum last_blend_src_alpha; glGetIntegerv(GL_BLEND_SRC_ALPHA, (GLint*)&last_blend_src_alpha);
    GLenum last_blend_dst_alpha; glGetIntegerv(GL_BLEND_DST_ALPHA, (GLint*)&last_blend_dst_alpha);
    GLenum last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, (GLint*)&last_blend_equation_rgb);
    GLenum last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, (GLint*)&last_blend_equation_alpha);
    GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
    GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
    GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean last_enable_stencil_test = glIsEnabled(GL_STENCIL_TEST);
    GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_PRIMITIVE_RESTART
    GLboolean last_enable_primitive_restart = (!bd->GlProfileIsES3 && bd->GlVersion >= 310) ? glIsEnabled(GL_PRIMITIVE_RESTART) : GL_FALSE;
#endif

    // Setup desired GL state
    GLuint vertex_array_object = 0;
#ifdef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
    GL_CALL(glGenVertexArrays(1, &vertex_array_object));
#endif
    ImGui_ImplOpenGL3Slang_SetupRenderState(draw_data, fb_width, fb_height, vertex_array_object);

    ImVec2 clip_off = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;

    // Build command list from ImGui draw commands (order-preserving).
    std::vector<ImGuiRenderCore::UniformBlockUpdate> pendingUniforms = bd->FrameCommands->uniformUpdates;
    bd->FrameCommands->Clear();
    for (const ImGuiRenderCore::UniformBlockUpdate& u : pendingUniforms)
        bd->FrameCommands->PushUniformUpdate(u);
    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            ImGuiRenderCore::DrawPacket packet;
            packet.pipelineKey = "imgui_default";
            packet.texture = pcmd->GetTexID();
            packet.viewportId = draw_data->OwnerViewport ? draw_data->OwnerViewport->ID : 0;
            packet.clipRect = pcmd->ClipRect;
            packet.elemCount = (int)pcmd->ElemCount;
            packet.idxOffset = (int)pcmd->IdxOffset;
            packet.vtxOffset = (int)pcmd->VtxOffset;
            packet.drawList = draw_list;
            packet.drawCmd = pcmd;
            packet.isImGuiPacket = true;
            bd->FrameCommands->PushPacket(packet);
        }
    }

    // Apply external uniform block updates (if any)
    for (const ImGuiRenderCore::UniformBlockUpdate& update : bd->FrameCommands->uniformUpdates)
    {
        if (update.binding == 1 && bd->UboHandle != 0 && !update.bytes.empty())
        {
            GL_CALL(glBindBuffer(GL_UNIFORM_BUFFER, bd->UboHandle));
            GL_CALL(glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)update.bytes.size(), update.bytes.data()));
            glBindBufferBase(GL_UNIFORM_BUFFER, update.binding, bd->UboHandle);
        }
    }

    // Append custom passes relevant to current draw_data.
    // Keep packets from other viewports for subsequent RenderDrawData() calls.
    const ImGuiID currentViewportId = draw_data->OwnerViewport ? draw_data->OwnerViewport->ID : 0;
    std::unordered_set<const ImDrawList*> drawListsInThisViewport;
    drawListsInThisViewport.reserve((size_t)draw_data->CmdListsCount);
    for (int n = 0; n < draw_data->CmdListsCount; ++n)
        drawListsInThisViewport.insert(draw_data->CmdLists[n]);

    for (ImGuiRenderCore::CustomPassData& pass : bd->CustomPasses->ConsumePasses())
    {
        for (const ImGuiRenderCore::DrawPacket& packet : pass.packets)
        {
            const bool viewportMatch = (packet.viewportId != 0) ? (packet.viewportId == currentViewportId) : true;
            const bool drawListMatch = (packet.drawList != nullptr && drawListsInThisViewport.find(packet.drawList) != drawListsInThisViewport.end());
            if (viewportMatch && (packet.viewportId != 0 || drawListMatch))
            {
                bd->FrameCommands->PushPacket(packet);
            }
            else
            {
                bd->CustomPasses->PushPacket(pass.passKey, packet);
            }
        }
    }

    const ImDrawList* lastUploadedDrawList = nullptr;
    for (const ImGuiRenderCore::DrawPacket& packet : bd->FrameCommands->packets)
    {
        const ImDrawList* draw_list = packet.drawList;
        const ImDrawCmd* pcmd = packet.drawCmd;
        if (!draw_list)
            continue;

        if (bd->GlAdapter)
        {
            auto itPipeline = g_Pipelines.find(packet.pipelineKey);
            if (itPipeline != g_Pipelines.end())
            {
                auto itBlend = g_BlendModes.find(itPipeline->second.blendModeKey);
                if (itBlend != g_BlendModes.end())
                    bd->GlAdapter->ApplyBlendState(itBlend->second);

                auto itProgram = g_ShaderPrograms.find(itPipeline->second.shaderKey);
                if (itProgram != g_ShaderPrograms.end() && itProgram->second.program != 0)
                {
                    bd->GlAdapter->BindProgram(itProgram->second.program);
                    if (itProgram->second.textureLocation >= 0)
                        glUniform1i(itProgram->second.textureLocation, 0);
                    if (bd->UboHandle)
                        glBindBufferBase(GL_UNIFORM_BUFFER, 1, bd->UboHandle);
                    if (!packet.effectUniformBytes.empty() && packet.effectUniformBinding != 0 && bd->EffectParamsUbo != 0)
                    {
                        GLsizeiptr ubo_sz = (GLsizeiptr)packet.effectUniformBytes.size();
                        if (ubo_sz > 256)
                            ubo_sz = 256;
                        GL_CALL(glBindBuffer(GL_UNIFORM_BUFFER, bd->EffectParamsUbo));
                        GL_CALL(glBufferSubData(GL_UNIFORM_BUFFER, 0, ubo_sz, packet.effectUniformBytes.data()));
                        glBindBufferBase(GL_UNIFORM_BUFFER, (GLuint)packet.effectUniformBinding, bd->EffectParamsUbo);
                    }
                }
            }
        }

        if (draw_list != lastUploadedDrawList)
        {
            const GLsizeiptr vtx_buffer_size = (GLsizeiptr)draw_list->VtxBuffer.Size * (int)sizeof(ImDrawVert);
            const GLsizeiptr idx_buffer_size = (GLsizeiptr)draw_list->IdxBuffer.Size * (int)sizeof(ImDrawIdx);
            if (bd->UseBufferSubData)
            {
                if (bd->VertexBufferSize < vtx_buffer_size)
                {
                    bd->VertexBufferSize = vtx_buffer_size;
                    GL_CALL(glBufferData(GL_ARRAY_BUFFER, bd->VertexBufferSize, nullptr, GL_STREAM_DRAW));
                }
                if (bd->IndexBufferSize < idx_buffer_size)
                {
                    bd->IndexBufferSize = idx_buffer_size;
                    GL_CALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, bd->IndexBufferSize, nullptr, GL_STREAM_DRAW));
                }
                GL_CALL(glBufferSubData(GL_ARRAY_BUFFER, 0, vtx_buffer_size, (const GLvoid*)draw_list->VtxBuffer.Data));
                GL_CALL(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_buffer_size, (const GLvoid*)draw_list->IdxBuffer.Data));
            }
            else
            {
                GL_CALL(glBufferData(GL_ARRAY_BUFFER, vtx_buffer_size, (const GLvoid*)draw_list->VtxBuffer.Data, GL_STREAM_DRAW));
                GL_CALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size, (const GLvoid*)draw_list->IdxBuffer.Data, GL_STREAM_DRAW));
            }
            lastUploadedDrawList = draw_list;
        }

        if (packet.isImGuiPacket && pcmd && pcmd->UserCallback != nullptr)
        {
            if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                ImGui_ImplOpenGL3Slang_SetupRenderState(draw_data, fb_width, fb_height, vertex_array_object);
            else
                pcmd->UserCallback(draw_list, pcmd);
            continue;
        }

        ImVec2 clip_min((packet.clipRect.x - clip_off.x) * clip_scale.x, (packet.clipRect.y - clip_off.y) * clip_scale.y);
        ImVec2 clip_max((packet.clipRect.z - clip_off.x) * clip_scale.x, (packet.clipRect.w - clip_off.y) * clip_scale.y);
        if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
            continue;
        GL_CALL(glScissor((int)clip_min.x, (int)((float)fb_height - clip_max.y), (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y)));

        if (bd->GlAdapter)
            bd->GlAdapter->BindTexture2D(0, (GLuint)(intptr_t)packet.texture);
        else
        {
            glActiveTexture(GL_TEXTURE0);
            GL_CALL(glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)packet.texture));
        }

#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET
        if (bd->GlVersion >= 320)
        {
            if (bd->GlAdapter)
                bd->GlAdapter->DrawElements((GLsizei)packet.elemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(packet.idxOffset * sizeof(ImDrawIdx)), (GLint)packet.vtxOffset, true);
            else
                GL_CALL(glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)packet.elemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(packet.idxOffset * sizeof(ImDrawIdx)), (GLint)packet.vtxOffset));
        }
        else
#endif
        {
            if (bd->GlAdapter)
                bd->GlAdapter->DrawElements((GLsizei)packet.elemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(packet.idxOffset * sizeof(ImDrawIdx)), 0, false);
            else
                GL_CALL(glDrawElements(GL_TRIANGLES, (GLsizei)packet.elemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(packet.idxOffset * sizeof(ImDrawIdx))));
        }
    }
    if (bd->GlAdapter)
    {
        const ImGuiRenderCore::GlAdapterStats& stats = bd->GlAdapter->GetStats();
        g_LastStats.programBinds = stats.programBinds;
        g_LastStats.textureBinds = stats.textureBinds;
        g_LastStats.blendStateChanges = stats.blendStateChanges;
        g_LastStats.drawCalls = stats.drawCalls;
    }

#ifdef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
    GL_CALL(glDeleteVertexArrays(1, &vertex_array_object));
#endif

    // Restore modified GL state
    if (last_program == 0 || glIsProgram(last_program)) glUseProgram(last_program);
    glBindTexture(GL_TEXTURE_2D, last_texture);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_SAMPLER
    if (bd->HasBindSampler)
        glBindSampler(0, last_sampler);
#endif
    glActiveTexture(last_active_texture);
#ifdef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
    glBindVertexArray(last_vertex_array_object);
#endif
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
#ifndef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, last_element_array_buffer);
    last_vtx_attrib_state_pos.SetState(bd->AttribLocationVtxPos);
    last_vtx_attrib_state_uv.SetState(bd->AttribLocationVtxUV);
    last_vtx_attrib_state_color.SetState(bd->AttribLocationVtxColor);
#endif
    glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
    glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha);
    if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (last_enable_stencil_test) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_PRIMITIVE_RESTART
    if (!bd->GlProfileIsES3 && bd->GlVersion >= 310) { if (last_enable_primitive_restart) glEnable(GL_PRIMITIVE_RESTART); else glDisable(GL_PRIMITIVE_RESTART); }
#endif

#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE
    if (bd->HasPolygonMode) { if (bd->GlVersion <= 310 || bd->GlProfileIsCompat) { glPolygonMode(GL_FRONT, (GLenum)last_polygon_mode[0]); glPolygonMode(GL_BACK, (GLenum)last_polygon_mode[1]); } else { glPolygonMode(GL_FRONT_AND_BACK, (GLenum)last_polygon_mode[0]); } }
#endif

    glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
    (void)bd;
}

static void ImGui_ImplOpenGL3Slang_DestroyTexture(ImTextureData* tex)
{
    GLuint gl_tex_id = (GLuint)(intptr_t)tex->TexID;
    glDeleteTextures(1, &gl_tex_id);

    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
}

void ImGui_ImplOpenGL3Slang_UpdateTexture(ImTextureData* tex)
{
    if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates)
    {
#ifdef GL_UNPACK_ROW_LENGTH
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
#endif
#ifdef GL_UNPACK_ALIGNMENT
        GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
#endif
    }

    if (tex->Status == ImTextureStatus_WantCreate)
    {
        IM_ASSERT(tex->TexID == 0 && tex->BackendUserData == nullptr);
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
        const void* pixels = tex->GetPixels();
        GLuint gl_texture_id = 0;

        GLint last_texture;
        GL_CALL(glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
        GL_CALL(glGenTextures(1, &gl_texture_id));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, gl_texture_id));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->Width, tex->Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

        tex->SetTexID((ImTextureID)(intptr_t)gl_texture_id);
        tex->SetStatus(ImTextureStatus_OK);

        GL_CALL(glBindTexture(GL_TEXTURE_2D, last_texture));
    }
    else if (tex->Status == ImTextureStatus_WantUpdates)
    {
        GLint last_texture;
        GL_CALL(glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));

        GLuint gl_tex_id = (GLuint)(intptr_t)tex->TexID;
        GL_CALL(glBindTexture(GL_TEXTURE_2D, gl_tex_id));
#if defined(GL_UNPACK_ROW_LENGTH)
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->Width));
        for (ImTextureRect& r : tex->Updates)
            GL_CALL(glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h, GL_RGBA, GL_UNSIGNED_BYTE, tex->GetPixelsAt(r.x, r.y)));
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
#else
        ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
        for (ImTextureRect& r : tex->Updates)
        {
            const int src_pitch = r.w * tex->BytesPerPixel;
            bd->TempBuffer.resize(r.h * src_pitch);
            char* out_p = bd->TempBuffer.Data;
            for (int y = 0; y < r.h; y++, out_p += src_pitch)
                memcpy(out_p, tex->GetPixelsAt(r.x, r.y + y), src_pitch);
            IM_ASSERT(out_p == bd->TempBuffer.end());
            GL_CALL(glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h, GL_RGBA, GL_UNSIGNED_BYTE, bd->TempBuffer.Data));
        }
#endif
        tex->SetStatus(ImTextureStatus_OK);
        GL_CALL(glBindTexture(GL_TEXTURE_2D, last_texture));
    }
    else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
        ImGui_ImplOpenGL3Slang_DestroyTexture(tex);
}

static bool CheckShader(GLuint handle, const char* desc)
{
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
    GLint status = 0, log_length = 0;
    glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
    glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_length);
    if ((GLboolean)status == GL_FALSE)
        fprintf(stderr, "ERROR: ImGui_ImplOpenGL3Slang_CreateDeviceObjects: failed to compile %s! With GLSL: %s\n", desc, bd->GlslVersionString);
    if (log_length > 1)
    {
        ImVector<char> buf;
        buf.resize((int)(log_length + 1));
        glGetShaderInfoLog(handle, log_length, nullptr, (GLchar*)buf.begin());
        fprintf(stderr, "%s\n", buf.begin());
    }
    return (GLboolean)status == GL_TRUE;
}

static bool CheckProgram(GLuint handle, const char* desc)
{
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
    GLint status = 0, log_length = 0;
    glGetProgramiv(handle, GL_LINK_STATUS, &status);
    glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &log_length);
    if ((GLboolean)status == GL_FALSE)
        fprintf(stderr, "ERROR: ImGui_ImplOpenGL3Slang_CreateDeviceObjects: failed to link %s! With GLSL %s\n", desc, bd->GlslVersionString);
    if (log_length > 1)
    {
        ImVector<char> buf;
        buf.resize((int)(log_length + 1));
        glGetProgramInfoLog(handle, log_length, nullptr, (GLchar*)buf.begin());
        fprintf(stderr, "%s\n", buf.begin());
    }
    return (GLboolean)status == GL_TRUE;
}

static bool CreateProgramFromGLSL(const char* vertexShaderGLSL, const char* fragmentShaderGLSL, ShaderProgramState* outState)
{
    if (!vertexShaderGLSL || !fragmentShaderGLSL || !outState)
        return false;

    GLuint vert_handle;
    GL_CALL(vert_handle = glCreateShader(GL_VERTEX_SHADER));
    glShaderSource(vert_handle, 1, &vertexShaderGLSL, nullptr);
    glCompileShader(vert_handle);
    if (!CheckShader(vert_handle, "vertex shader"))
    {
        glDeleteShader(vert_handle);
        return false;
    }

    GLuint frag_handle;
    GL_CALL(frag_handle = glCreateShader(GL_FRAGMENT_SHADER));
    glShaderSource(frag_handle, 1, &fragmentShaderGLSL, nullptr);
    glCompileShader(frag_handle);
    if (!CheckShader(frag_handle, "fragment shader"))
    {
        glDeleteShader(vert_handle);
        glDeleteShader(frag_handle);
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert_handle);
    glAttachShader(program, frag_handle);
    glLinkProgram(program);
    if (!CheckProgram(program, "shader program"))
    {
        glDeleteShader(vert_handle);
        glDeleteShader(frag_handle);
        glDeleteProgram(program);
        return false;
    }

    glDetachShader(program, vert_handle);
    glDetachShader(program, frag_handle);
    glDeleteShader(vert_handle);
    glDeleteShader(frag_handle);

    outState->program = program;
    outState->textureLocation = glGetUniformLocation(program, "Texture_0");
    GLuint blockIndex = glGetUniformBlockIndex(program, "block_GlobalParams_0");
    if (blockIndex != GL_INVALID_INDEX)
        glUniformBlockBinding(program, blockIndex, 1);
    GLuint effectBlockIndex = glGetUniformBlockIndex(program, "block_EffectParams_0");
    if (effectBlockIndex != GL_INVALID_INDEX)
        glUniformBlockBinding(program, effectBlockIndex, 2);
    return true;
}

bool    ImGui_ImplOpenGL3Slang_CreateDeviceObjects()
{
    ImGui_ImplOpenGL3Slang_InitLoader();
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();

    // Backup GL state
    GLint last_texture, last_array_buffer;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_BUFFER_PIXEL_UNPACK
    GLint last_pixel_unpack_buffer = 0;
    if (bd->GlVersion >= 210) { glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &last_pixel_unpack_buffer); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); }
#endif
#ifdef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
    GLint last_vertex_array;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);
#endif

    // Slang shader source (HLSL/Slang syntax) - matches ImGui layout: ProjMtx, Position/UV/Color, Texture, Frag_UV/Frag_Color
    const char* imguiSlangSource = R"slang(
struct VSOutput {
    float2 Frag_UV : TEXCOORD0;
    float4 Frag_Color : COLOR0;
    float4 Position : SV_Position;
};

uniform float4x4 ProjMtx;

[shader("vertex")]
VSOutput vertexMain(
    float2 Position : POSITION0,
    float2 UV : TEXCOORD0,
    float4 Color : COLOR0)
{
    VSOutput output;
    output.Frag_UV = UV;
    output.Frag_Color = Color;
    output.Position = mul(ProjMtx, float4(Position, 0.0, 1.0));  // matrix * vector (OpenGL)
    return output;
}

// Use combined Sampler2D; [[vk::binding(0)]] maps to texture unit 0 in GLSL
[[vk::binding(0)]] Sampler2D Texture_0;

[shader("fragment")]
float4 fragmentMain(
    float2 Frag_UV : TEXCOORD0,
    float4 Frag_Color : COLOR0,
    float4 Position : SV_Position) : SV_Target
{
    return Frag_Color * Texture_0.Sample(Frag_UV);
}
)slang";

    // Create Slang global session and session with GLSL 330 target
    Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
    SLANG_RETURN_FALSE_ON_FAIL(slang_createGlobalSession(SLANG_API_VERSION, slangGlobalSession.writeRef()));

    // Use glsl_450 - Slang emits layout(binding)/layout(location) which need 420+
    SlangProfileID glslProfile = slangGlobalSession->findProfile("glsl_450");
    if (glslProfile == SLANG_PROFILE_UNKNOWN)
        glslProfile = slangGlobalSession->findProfile("glsl_430");
    if (glslProfile == SLANG_PROFILE_UNKNOWN)
        glslProfile = slangGlobalSession->findProfile("glsl_330");
    if (glslProfile == SLANG_PROFILE_UNKNOWN)
    {
        fprintf(stderr, "ERROR: Slang GLSL profile not found\n");
        return false;
    }

    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_GLSL;
    targetDesc.profile = glslProfile;

    slang::SessionDesc sessionDesc = {};
    sessionDesc.targetCount = 1;
    sessionDesc.targets = &targetDesc;
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;  // OpenGL
    Slang::ComPtr<slang::ISession> slangSession;
    SLANG_RETURN_FALSE_ON_FAIL(slangGlobalSession->createSession(sessionDesc, slangSession.writeRef()));

    // Load module from source
    Slang::ComPtr<slang::IBlob> loadDiag;
    Slang::ComPtr<slang::IModule> slangModule;
    slangModule = slangSession->loadModuleFromSourceString("imgui", "imgui.slang", imguiSlangSource, loadDiag.writeRef());
    if (!slangModule)
    {
        if (loadDiag)
            fprintf(stderr, "Slang load error:\n%s\n", (const char*)loadDiag->getBufferPointer());
        else
            fprintf(stderr, "ERROR: Slang failed to load imgui shader module\n");
        return false;
    }

    // Find vertex and fragment entry points
    Slang::ComPtr<slang::IEntryPoint> vertexEntryPoint;
    Slang::ComPtr<slang::IEntryPoint> fragmentEntryPoint;
    SLANG_RETURN_FALSE_ON_FAIL(slangModule->findEntryPointByName("vertexMain", vertexEntryPoint.writeRef()));
    SLANG_RETURN_FALSE_ON_FAIL(slangModule->findEntryPointByName("fragmentMain", fragmentEntryPoint.writeRef()));

    // Create composite of both entry points
    slang::IComponentType* componentTypes[] = { vertexEntryPoint.get(), fragmentEntryPoint.get() };
    Slang::ComPtr<slang::IComponentType> compositeType;
    SLANG_RETURN_FALSE_ON_FAIL(slangSession->createCompositeComponentType(componentTypes, 2, compositeType.writeRef()));

    // Link
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    SLANG_RETURN_FALSE_ON_FAIL(compositeType->link(linkedProgram.writeRef()));

    // Get GLSL code for each entry point (0=vertex, 1=fragment)
    Slang::ComPtr<slang::IBlob> vertexCodeBlob;
    Slang::ComPtr<slang::IBlob> fragmentCodeBlob;
    Slang::ComPtr<slang::IBlob> diagnostics;
    if (SLANG_FAILED(linkedProgram->getEntryPointCode(0, 0, vertexCodeBlob.writeRef(), diagnostics.writeRef())))
    {
        if (diagnostics)
            fprintf(stderr, "Slang vertex shader error:\n%s\n", (const char*)diagnostics->getBufferPointer());
        return false;
    }
    if (SLANG_FAILED(linkedProgram->getEntryPointCode(1, 0, fragmentCodeBlob.writeRef(), diagnostics.writeRef())))
    {
        if (diagnostics)
            fprintf(stderr, "Slang fragment shader error:\n%s\n", (const char*)diagnostics->getBufferPointer());
        return false;
    }

    const char* vertexShaderGLSL = (const char*)vertexCodeBlob->getBufferPointer();
    const char* fragmentShaderGLSL = (const char*)fragmentCodeBlob->getBufferPointer();

#ifdef IMGUI_IMPL_OPENGL_DEBUG
    fprintf(stderr, "Slang vertex shader:\n%s\n", vertexShaderGLSL);
    fprintf(stderr, "Slang fragment shader:\n%s\n", fragmentShaderGLSL);
#endif

    // Create OpenGL shaders from compiled GLSL (Slang already includes #version in output)
    GLuint vert_handle;
    GL_CALL(vert_handle = glCreateShader(GL_VERTEX_SHADER));
    glShaderSource(vert_handle, 1, &vertexShaderGLSL, nullptr);
    glCompileShader(vert_handle);
    if (!CheckShader(vert_handle, "vertex shader"))
        return false;

    GLuint frag_handle;
    GL_CALL(frag_handle = glCreateShader(GL_FRAGMENT_SHADER));
    glShaderSource(frag_handle, 1, &fragmentShaderGLSL, nullptr);
    glCompileShader(frag_handle);
    if (!CheckShader(frag_handle, "fragment shader"))
        return false;

    // Link
    bd->ShaderHandle = glCreateProgram();
    glAttachShader(bd->ShaderHandle, vert_handle);
    glAttachShader(bd->ShaderHandle, frag_handle);
    glLinkProgram(bd->ShaderHandle);
    if (!CheckProgram(bd->ShaderHandle, "shader program"))
        return false;

    glDetachShader(bd->ShaderHandle, vert_handle);
    glDetachShader(bd->ShaderHandle, frag_handle);
    glDeleteShader(vert_handle);
    glDeleteShader(frag_handle);

    // Slang-generated names: Texture_0, Position_0, UV_0, Color_0; ProjMtx in block_GlobalParams_0
    bd->AttribLocationTex = glGetUniformLocation(bd->ShaderHandle, "Texture_0");
    bd->AttribLocationVtxPos = (GLuint)glGetAttribLocation(bd->ShaderHandle, "Position_0");
    bd->AttribLocationVtxUV = (GLuint)glGetAttribLocation(bd->ShaderHandle, "UV_0");
    bd->AttribLocationVtxColor = (GLuint)glGetAttribLocation(bd->ShaderHandle, "Color_0");
    g_ShaderPrograms["imgui_default"] = { bd->ShaderHandle, bd->AttribLocationTex };

    // Create UBO for projection matrix (Slang uniform block block_GlobalParams_0, binding=1)
    GLuint blockIndex = glGetUniformBlockIndex(bd->ShaderHandle, "block_GlobalParams_0");
    if (blockIndex != GL_INVALID_INDEX)
    {
        glUniformBlockBinding(bd->ShaderHandle, blockIndex, 1);
        glGenBuffers(1, &bd->UboHandle);
        glBindBuffer(GL_UNIFORM_BUFFER, bd->UboHandle);
        glBufferData(GL_UNIFORM_BUFFER, 64, nullptr, GL_DYNAMIC_DRAW); // 4x4 matrix
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    if (bd->EffectParamsUbo == 0)
    {
        glGenBuffers(1, &bd->EffectParamsUbo);
        glBindBuffer(GL_UNIFORM_BUFFER, bd->EffectParamsUbo);
        glBufferData(GL_UNIFORM_BUFFER, 256, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    // Create buffers
    glGenBuffers(1, &bd->VboHandle);
    glGenBuffers(1, &bd->ElementsHandle);

    // Restore modified GL state
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_BUFFER_PIXEL_UNPACK
    if (bd->GlVersion >= 210) { glBindBuffer(GL_PIXEL_UNPACK_BUFFER, last_pixel_unpack_buffer); }
#endif
#ifdef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
    glBindVertexArray(last_vertex_array);
#endif

    return true;
}

void    ImGui_ImplOpenGL3Slang_DestroyDeviceObjects()
{
    ImGui_ImplOpenGL3Slang_InitLoader();
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
    if (bd->VboHandle)      { glDeleteBuffers(1, &bd->VboHandle); bd->VboHandle = 0; }
    if (bd->ElementsHandle) { glDeleteBuffers(1, &bd->ElementsHandle); bd->ElementsHandle = 0; }
    if (bd->UboHandle)      { glDeleteBuffers(1, &bd->UboHandle); bd->UboHandle = 0; }
    if (bd->EffectParamsUbo) { glDeleteBuffers(1, &bd->EffectParamsUbo); bd->EffectParamsUbo = 0; }
    for (auto& it : g_ShaderPrograms)
    {
        if (it.second.program != 0 && it.second.program != bd->ShaderHandle)
            glDeleteProgram(it.second.program);
    }
    g_ShaderPrograms.clear();
    if (bd->ShaderHandle)   { glDeleteProgram(bd->ShaderHandle); bd->ShaderHandle = 0; }

    // Destroy all textures
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
            ImGui_ImplOpenGL3Slang_DestroyTexture(tex);
}

bool ImGui_ImplOpenGL3Slang_RegisterShaderProgram(const ImGuiRenderCore::ShaderDesc& shaderDesc, const char** errorText)
{
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
    if (!bd || !bd->ShaderManager)
    {
        if (errorText) *errorText = "Backend not initialized";
        return false;
    }
    std::string error;
    if (!bd->ShaderManager->RegisterOrUpdateShader(shaderDesc, &error))
    {
        g_LastErrorText = error;
        if (errorText) *errorText = g_LastErrorText.c_str();
        return false;
    }
    const ImGuiRenderCore::CompiledShader* compiled = bd->ShaderManager->FindCompiled(shaderDesc.shaderKey);
    if (!compiled)
    {
        g_LastErrorText = "Compiled shader not found in cache";
        if (errorText) *errorText = g_LastErrorText.c_str();
        return false;
    }

    ShaderProgramState state;
    if (!CreateProgramFromGLSL(compiled->vertexGLSL.c_str(), compiled->fragmentGLSL.c_str(), &state))
    {
        g_LastErrorText = "OpenGL program link failed for shader: " + shaderDesc.shaderKey;
        if (errorText) *errorText = g_LastErrorText.c_str();
        return false;
    }

    auto existing = g_ShaderPrograms.find(shaderDesc.shaderKey);
    if (existing != g_ShaderPrograms.end() && existing->second.program != 0)
        glDeleteProgram(existing->second.program);
    g_ShaderPrograms[shaderDesc.shaderKey] = state;
    g_Shaders[shaderDesc.shaderKey] = shaderDesc;
    if (errorText) *errorText = nullptr;
    return true;
}

void ImGui_ImplOpenGL3Slang_RegisterBlendMode(const char* blendKey, const ImGuiRenderCore::BlendStateDesc& blendDesc)
{
    if (!blendKey || blendKey[0] == '\0')
        return;
    g_BlendModes[blendKey] = blendDesc;
}

void ImGui_ImplOpenGL3Slang_RegisterPipeline(const ImGuiRenderCore::PipelineDesc& pipelineDesc)
{
    if (pipelineDesc.pipelineKey.empty())
        return;
    g_Pipelines[pipelineDesc.pipelineKey] = pipelineDesc;
}

void ImGui_ImplOpenGL3Slang_PushCustomDraw(const char* passKey, const ImGuiRenderCore::DrawPacket& drawPacket)
{
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
    if (!bd || !bd->CustomPasses || !passKey || passKey[0] == '\0')
        return;
    bd->CustomPasses->PushPacket(passKey, drawPacket);
}

void ImGui_ImplOpenGL3Slang_UnregisterEffectResources(const char* shaderKey, const char* pipelineKey)
{
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
    auto delete_program_safe = [&](GLuint program) {
        if (program == 0)
            return;
        if (bd != nullptr && program == bd->ShaderHandle)
            return;
        glDeleteProgram(program);
    };

    if (pipelineKey != nullptr && pipelineKey[0] != '\0')
    {
        if (strcmp(pipelineKey, "imgui_default") != 0)
            g_Pipelines.erase(pipelineKey);
    }
    if (shaderKey != nullptr && shaderKey[0] != '\0')
    {
        if (strcmp(shaderKey, "imgui_default") != 0)
        {
            auto itProg = g_ShaderPrograms.find(shaderKey);
            if (itProg != g_ShaderPrograms.end())
            {
                delete_program_safe(itProg->second.program);
                g_ShaderPrograms.erase(itProg);
            }
            g_Shaders.erase(shaderKey);
            if (bd != nullptr && bd->ShaderManager != nullptr)
                bd->ShaderManager->RemoveShader(shaderKey);
        }
    }
}

const char* ImGui_ImplOpenGL3Slang_GetLastError()
{
    return g_LastErrorText.c_str();
}

void ImGui_ImplOpenGL3Slang_SetGlobalUniformBlock(const char* blockName, uint32_t binding, const void* data, size_t bytes)
{
    ImGui_ImplOpenGL3Slang_Data* bd = ImGui_ImplOpenGL3Slang_GetBackendData();
    if (!bd || !bd->FrameCommands || !blockName || !data || bytes == 0)
        return;
    ImGuiRenderCore::UniformBlockUpdate update;
    update.name = blockName;
    update.binding = binding;
    update.bytes.resize(bytes);
    memcpy(update.bytes.data(), data, bytes);
    bd->FrameCommands->PushUniformUpdate(update);
}

ImGui_ImplOpenGL3Slang_Stats ImGui_ImplOpenGL3Slang_GetStats()
{
    return g_LastStats;
}

//--------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
//--------------------------------------------------------------------------------------------------------

static void ImGui_ImplOpenGL3Slang_RenderWindow(ImGuiViewport* viewport, void*)
{
    if (!(viewport->Flags & ImGuiViewportFlags_NoRendererClear))
    {
        ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    ImGui_ImplOpenGL3Slang_RenderDrawData(viewport->DrawData);
}

static void ImGui_ImplOpenGL3Slang_InitMultiViewportSupport()
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_RenderWindow = ImGui_ImplOpenGL3Slang_RenderWindow;
}

static void ImGui_ImplOpenGL3Slang_ShutdownMultiViewportSupport()
{
    ImGui::DestroyPlatformWindows();
}

//-----------------------------------------------------------------------------

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef IMGUI_DISABLE
