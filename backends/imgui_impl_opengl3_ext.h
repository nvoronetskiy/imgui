// Extension for imgui_impl_opengl3: SDF lines and Glow shader

#pragma once

#include "imgui.h"

#ifdef __cplusplus
extern "C" {
#endif

IMGUI_IMPL_API bool ImGui_ImplOpenGL3_Ext_Init(const char* glsl_version);
IMGUI_IMPL_API void ImGui_ImplOpenGL3_Ext_Shutdown(void);
IMGUI_IMPL_API ImTextureID ImGui_ImplOpenGL3_Ext_GetSdfTextureId(void);
IMGUI_IMPL_API ImTextureID ImGui_ImplOpenGL3_Ext_GetGlowTextureId(void);

IMGUI_IMPL_API bool ImGui_ImplOpenGL3_Ext_IsSdfTexture(ImTextureID tex_id);
IMGUI_IMPL_API bool ImGui_ImplOpenGL3_Ext_IsGlowTexture(ImTextureID tex_id);
IMGUI_IMPL_API void ImGui_ImplOpenGL3_Ext_ApplySdfState(ImDrawData* draw_data, int fb_width, int fb_height, unsigned int attrib_pos, unsigned int attrib_uv, unsigned int attrib_col);
IMGUI_IMPL_API void ImGui_ImplOpenGL3_Ext_ApplyGlowState(ImDrawData* draw_data, int fb_width, int fb_height, unsigned int attrib_pos, unsigned int attrib_uv, unsigned int attrib_col);

#ifdef __cplusplus
}
#endif
