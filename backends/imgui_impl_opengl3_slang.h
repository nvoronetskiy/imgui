// dear imgui: Renderer Backend for OpenGL3 with Slang shader compiler
// Same API as imgui_impl_opengl3.h, but uses Slang to compile shaders instead of embedded GLSL.
// Requires: slang (shader-slang) library

#pragma once
#include "imgui.h"
#include "imgui_render_core.h"
#ifndef IMGUI_DISABLE

IMGUI_IMPL_API bool     ImGui_ImplOpenGL3Slang_Init(const char* glsl_version = nullptr);
IMGUI_IMPL_API void     ImGui_ImplOpenGL3Slang_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplOpenGL3Slang_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplOpenGL3Slang_RenderDrawData(ImDrawData* draw_data);
IMGUI_IMPL_API bool     ImGui_ImplOpenGL3Slang_CreateDeviceObjects();
IMGUI_IMPL_API void     ImGui_ImplOpenGL3Slang_DestroyDeviceObjects();
IMGUI_IMPL_API void     ImGui_ImplOpenGL3Slang_UpdateTexture(ImTextureData* tex);

// Optional extensibility API (OpenGL-first, Vulkan-ready abstractions)
IMGUI_IMPL_API bool     ImGui_ImplOpenGL3Slang_RegisterShaderProgram(const ImGuiRenderCore::ShaderDesc& shaderDesc, const char** errorText = nullptr);
IMGUI_IMPL_API void     ImGui_ImplOpenGL3Slang_RegisterBlendMode(const char* blendKey, const ImGuiRenderCore::BlendStateDesc& blendDesc);
IMGUI_IMPL_API void     ImGui_ImplOpenGL3Slang_RegisterPipeline(const ImGuiRenderCore::PipelineDesc& pipelineDesc);
IMGUI_IMPL_API void     ImGui_ImplOpenGL3Slang_PushCustomDraw(const char* passKey, const ImGuiRenderCore::DrawPacket& drawPacket);
IMGUI_IMPL_API void     ImGui_ImplOpenGL3Slang_SetGlobalUniformBlock(const char* blockName, uint32_t binding, const void* data, size_t bytes);
// Release GPU program + pipeline registration for an effect (not the default ImGui shader).
IMGUI_IMPL_API void     ImGui_ImplOpenGL3Slang_UnregisterEffectResources(const char* shaderKey, const char* pipelineKey);
// Last error from RegisterShaderProgram / compililation (empty string if none).
IMGUI_IMPL_API const char* ImGui_ImplOpenGL3Slang_GetLastError();

struct ImGui_ImplOpenGL3Slang_Stats
{
    uint32_t programBinds = 0;
    uint32_t textureBinds = 0;
    uint32_t blendStateChanges = 0;
    uint32_t drawCalls = 0;
};
IMGUI_IMPL_API ImGui_ImplOpenGL3Slang_Stats ImGui_ImplOpenGL3Slang_GetStats();

#endif // #ifndef IMGUI_DISABLE
