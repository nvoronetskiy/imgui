#include "imgui_effect_helpers.h"
#include "imgui_internal.h"

#ifndef IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif
#include <GL/gl3w.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ImGuiRenderUX
{
namespace
{
long long FileTimeToTick(const std::filesystem::file_time_type& ft)
{
    return (long long)ft.time_since_epoch().count();
}

static const EffectTextureGLProcs* g_customTextureProcs = nullptr;

static const EffectTextureGLProcs& ActiveTextureProcs()
{
    static const EffectTextureGLProcs kDefaultGl3w = {
        [](int n, unsigned* textures) { glGenTextures((GLsizei)n, (GLuint*)textures); },
        [](int n, const unsigned* textures) { glDeleteTextures((GLsizei)n, (const GLuint*)textures); },
        [](unsigned target, unsigned texture) { glBindTexture((GLenum)target, (GLuint)texture); },
        [](unsigned target, unsigned pname, int param) { glTexParameteri((GLenum)target, (GLenum)pname, param); },
        [](unsigned target, int level, int internalformat, int width, int height, int border, unsigned format, unsigned type, const void* pixels) {
            glTexImage2D((GLenum)target, level, internalformat, width, height, border, (GLenum)format, (GLenum)type, pixels);
        },
    };
    return g_customTextureProcs ? *g_customTextureProcs : kDefaultGl3w;
}
} // namespace

void BuiltinGpuTextures::SetTextureProcs(const EffectTextureGLProcs* procs)
{
    g_customTextureProcs = procs;
}

void BuiltinGpuTextures::EnsureWhite1x1()
{
    if (m_glTex != 0)
        return;
    const EffectTextureGLProcs& gl = ActiveTextureProcs();
    unsigned char white[] = { 255, 255, 255, 255 };
    gl.GenTextures(1, &m_glTex);
    gl.BindTexture(GL_TEXTURE_2D, m_glTex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    m_whiteImgui = (ImTextureID)(intptr_t)m_glTex;
}

void BuiltinGpuTextures::Destroy()
{
    if (m_glTex != 0)
    {
        const EffectTextureGLProcs& gl = ActiveTextureProcs();
        gl.DeleteTextures(1, &m_glTex);
        m_glTex = 0;
        m_whiteImgui = ImTextureID{};
    }
}

bool EffectSystem::ReadTextFile(const std::string& path, std::string& outText)
{
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open())
        return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    outText = ss.str();
    return true;
}

bool EffectSystem::LoadShaderFileIntoDesc(EffectCreateDesc& ioDesc, std::string* errorText)
{
    if (!ioDesc.shaderSource.empty() || ioDesc.shaderFile.empty())
        return true;

    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    const fs::path rawPath(ioDesc.shaderFile);
    candidates.push_back(rawPath);
    if (rawPath.is_relative())
    {
        const fs::path cwd = fs::current_path();
        candidates.push_back(cwd / rawPath);
        candidates.push_back(cwd / ".." / rawPath);
        candidates.push_back(cwd / ".." / ".." / rawPath);
        candidates.push_back(cwd / ".." / ".." / ".." / rawPath);
    }

    bool loaded = false;
    std::string loadedFrom;
    for (const fs::path& p : candidates)
    {
        std::error_code ec;
        const fs::path normalized = fs::weakly_canonical(p, ec);
        const std::string tryPath = ec ? p.string() : normalized.string();
        if (ReadTextFile(tryPath, ioDesc.shaderSource))
        {
            loaded = true;
            loadedFrom = tryPath;
            break;
        }
    }

    if (!loaded)
    {
        if (errorText)
            *errorText = "Failed to read shader file: " + ioDesc.shaderFile + " (cwd: " + fs::current_path().string() + ")";
        return false;
    }
    ioDesc.shaderFile = loadedFrom;
    return true;
}

const char* EffectSystem::BlendKey(BuiltinBlendMode mode)
{
    switch (mode)
    {
    case BuiltinBlendMode::Alpha: return "ux_alpha";
    case BuiltinBlendMode::Additive: return "ux_additive";
    case BuiltinBlendMode::Multiply: return "ux_multiply";
    case BuiltinBlendMode::PremultipliedAlpha: return "ux_premultiplied_alpha";
    default: return "ux_alpha";
    }
}

ImGuiRenderCore::BlendStateDesc EffectSystem::BlendDesc(BuiltinBlendMode mode)
{
    ImGuiRenderCore::BlendStateDesc desc;
    switch (mode)
    {
    case BuiltinBlendMode::Alpha:
        break;
    case BuiltinBlendMode::Additive:
        desc.srcColor = ImGuiRenderCore::BlendFactor::One;
        desc.dstColor = ImGuiRenderCore::BlendFactor::One;
        desc.srcAlpha = ImGuiRenderCore::BlendFactor::One;
        desc.dstAlpha = ImGuiRenderCore::BlendFactor::OneMinusSrcAlpha;
        break;
    case BuiltinBlendMode::Multiply:
        desc.srcColor = ImGuiRenderCore::BlendFactor::DstColor;
        desc.dstColor = ImGuiRenderCore::BlendFactor::Zero;
        desc.srcAlpha = ImGuiRenderCore::BlendFactor::One;
        desc.dstAlpha = ImGuiRenderCore::BlendFactor::OneMinusSrcAlpha;
        break;
    case BuiltinBlendMode::PremultipliedAlpha:
        desc.srcColor = ImGuiRenderCore::BlendFactor::One;
        desc.dstColor = ImGuiRenderCore::BlendFactor::OneMinusSrcAlpha;
        desc.srcAlpha = ImGuiRenderCore::BlendFactor::One;
        desc.dstAlpha = ImGuiRenderCore::BlendFactor::OneMinusSrcAlpha;
        break;
    default:
        break;
    }
    return desc;
}

long long EffectSystem::QueryFileMtimeTicks(const std::string& path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec))
        return 0;
    const auto ft = fs::last_write_time(path, ec);
    if (ec)
        return 0;
    return FileTimeToTick(ft);
}

void EffectSystem::FrameContractTrace(const char* message) const
{
    if (!m_frameContractTracing || !message)
        return;
    std::fprintf(stderr, "[ImGuiRenderUX::EffectSystem] %s\n", message);
}

void EffectSystem::TouchUiPhaseForCaptures()
{
    if (m_framePhase == EffectFramePhase::AfterImGuiNewFrame || m_framePhase == EffectFramePhase::UiCapturing)
        m_framePhase = EffectFramePhase::UiCapturing;
}

bool EffectSystem::Initialize()
{
    if (m_initialized)
        return true;

    ImGui_ImplOpenGL3Slang_RegisterBlendMode(BlendKey(BuiltinBlendMode::Alpha), BlendDesc(BuiltinBlendMode::Alpha));
    ImGui_ImplOpenGL3Slang_RegisterBlendMode(BlendKey(BuiltinBlendMode::Additive), BlendDesc(BuiltinBlendMode::Additive));
    ImGui_ImplOpenGL3Slang_RegisterBlendMode(BlendKey(BuiltinBlendMode::Multiply), BlendDesc(BuiltinBlendMode::Multiply));
    ImGui_ImplOpenGL3Slang_RegisterBlendMode(BlendKey(BuiltinBlendMode::PremultipliedAlpha), BlendDesc(BuiltinBlendMode::PremultipliedAlpha));
    m_initialized = true;
    return true;
}

EffectSystem::EffectMeta* EffectSystem::FindEffect(EffectHandle handle)
{
    if (!handle.IsValid())
        return nullptr;
    auto it = m_effects.find(handle.id);
    if (it == m_effects.end())
        return nullptr;
    if (it->second.generation != handle.generation)
        return nullptr;
    return &it->second;
}

const EffectSystem::EffectMeta* EffectSystem::FindEffect(EffectHandle handle) const
{
    return const_cast<EffectSystem*>(this)->FindEffect(handle);
}

bool EffectSystem::IsEffectHandleLive(EffectHandle handle) const
{
    return FindEffect(handle) != nullptr;
}

EffectHandle EffectSystem::FindEffectByName(const std::string& name) const
{
    auto it = m_effectsByName.find(name);
    if (it == m_effectsByName.end())
        return kInvalidEffectHandle;
    auto mit = m_effects.find(it->second);
    if (mit == m_effects.end())
        return kInvalidEffectHandle;
    return EffectHandle{mit->second.id, mit->second.generation};
}

void EffectSystem::RegisterEffectName(const std::string& lookupName, uint32_t id)
{
    if (lookupName.empty())
        return;
    m_effectsByName[lookupName] = id;
}

void EffectSystem::UnregisterEffectName(const std::string& lookupName)
{
    if (lookupName.empty())
        return;
    m_effectsByName.erase(lookupName);
}

EffectHandle EffectSystem::EnsureEffect(const EffectCreateDesc& desc, std::string* errorText)
{
    if (!desc.name.empty())
    {
        auto it = m_effectsByName.find(desc.name);
        if (it != m_effectsByName.end())
        {
            auto mit = m_effects.find(it->second);
            if (mit != m_effects.end())
                return EffectHandle{mit->second.id, mit->second.generation};
            m_effectsByName.erase(it);
        }
    }
    return CreateEffect(desc, errorText);
}

void EffectSystem::UpdateShaderMtimeForMeta(EffectMeta& meta)
{
    if (meta.storedDesc.shaderFile.empty())
    {
        meta.hasShaderFileMtime = false;
        meta.shaderFileMtimeTick = 0;
        return;
    }
    meta.shaderFileMtimeTick = QueryFileMtimeTicks(meta.storedDesc.shaderFile);
    meta.hasShaderFileMtime = meta.shaderFileMtimeTick != 0;
}

EffectHandle EffectSystem::CreateEffectFromFile(const EffectCreateDesc& desc, std::string* errorText)
{
    m_lastCreateError.clear();
    EffectCreateDesc resolved = desc;
    if (!LoadShaderFileIntoDesc(resolved, errorText))
    {
        m_lastCreateError = errorText ? *errorText : std::string("Failed to read shader file");
        return kInvalidEffectHandle;
    }

    EffectHandle h = CreateEffect(resolved, errorText);
    if (h.IsValid())
    {
        EffectMeta* meta = FindEffect(h);
        if (meta)
        {
            meta->storedDesc.shaderFile = resolved.shaderFile;
            UpdateShaderMtimeForMeta(*meta);
        }
    }
    return h;
}

EffectHandle EffectSystem::CreateEffect(const EffectCreateDesc& desc, std::string* errorText)
{
    m_lastCreateError.clear();
    if (!m_initialized)
        Initialize();

    if (!desc.name.empty())
    {
        const auto itName = m_effectsByName.find(desc.name);
        if (itName != m_effectsByName.end())
        {
            if (m_effects.find(itName->second) == m_effects.end())
                m_effectsByName.erase(desc.name);
            else
            {
                if (errorText)
                    *errorText = "Effect name already in use: " + desc.name + " (use EnsureEffect or DestroyEffect first)";
                m_lastCreateError = errorText ? *errorText : std::string("duplicate effect name");
                return kInvalidEffectHandle;
            }
        }
    }

    if (desc.shaderSource.empty())
    {
        if (errorText)
            *errorText = "Effect shaderSource is empty";
        m_lastCreateError = "Effect shaderSource is empty";
        return kInvalidEffectHandle;
    }

    const uint32_t id = m_nextId++;
    const std::string baseName = desc.name.empty() ? ("effect_" + std::to_string(id)) : desc.name;
    const std::string shaderKey = "ux_shader_" + baseName + "_" + std::to_string(id);
    const std::string pipelineKey = "ux_pipeline_" + baseName + "_" + std::to_string(id);
    const std::string passKey = desc.passKey.empty() ? "OverlayPass" : desc.passKey;

    ImGuiRenderCore::ShaderDesc shaderDesc;
    shaderDesc.shaderKey = shaderKey;
    shaderDesc.source = desc.shaderSource;
    shaderDesc.vertexEntry = desc.vertexEntry;
    shaderDesc.fragmentEntry = desc.fragmentEntry;

    const char* cErr = nullptr;
    if (!ImGui_ImplOpenGL3Slang_RegisterShaderProgram(shaderDesc, &cErr))
    {
        if (errorText)
        {
            if (cErr && cErr[0])
                *errorText = cErr;
            else if (const char* last = ImGui_ImplOpenGL3Slang_GetLastError(); last && last[0])
                *errorText = last;
            else
                *errorText = "RegisterShaderProgram failed";
        }
        m_lastCreateError = (errorText && !errorText->empty()) ? *errorText : std::string("RegisterShaderProgram failed");
        return kInvalidEffectHandle;
    }

    ImGuiRenderCore::PipelineDesc pipelineDesc;
    pipelineDesc.pipelineKey = pipelineKey;
    pipelineDesc.shaderKey = shaderKey;
    pipelineDesc.blendModeKey = BlendKey(desc.blendMode);
    ImGui_ImplOpenGL3Slang_RegisterPipeline(pipelineDesc);

    EffectMeta meta;
    meta.id = id;
    meta.generation = 1;
    meta.name = baseName;
    meta.shaderKey = shaderKey;
    meta.pipelineKey = pipelineKey;
    meta.passKey = passKey;
    meta.storedDesc = desc;
    meta.storedDesc.shaderSource = desc.shaderSource;
    meta.effectUniformBinding = desc.effectUniformBinding;
    if (!desc.name.empty())
    {
        meta.registeredLookupName = desc.name;
        RegisterEffectName(desc.name, id);
    }
    UpdateShaderMtimeForMeta(meta);

    m_effects[id] = std::move(meta);
    return EffectHandle{id, 1u};
}

bool EffectSystem::DestroyEffect(EffectHandle handle)
{
    auto it = m_effects.find(handle.id);
    if (it == m_effects.end())
        return false;
    if (it->second.generation != handle.generation)
        return false;
    UnregisterEffectName(it->second.registeredLookupName);
    ImGui_ImplOpenGL3Slang_UnregisterEffectResources(it->second.shaderKey.c_str(), it->second.pipelineKey.c_str());
    m_effects.erase(it);
    return true;
}

EffectHandle EffectSystem::ReloadEffect(EffectHandle handle, std::string* errorText)
{
    if (errorText)
        errorText->clear();

    EffectMeta* meta = FindEffect(handle);
    if (!meta)
    {
        if (errorText)
            *errorText = "ReloadEffect: invalid or stale effect handle";
        m_lastCreateError = errorText ? *errorText : std::string("ReloadEffect: invalid handle");
        return kInvalidEffectHandle;
    }

    EffectCreateDesc desc = meta->storedDesc;
    if (!desc.shaderFile.empty())
        desc.shaderSource.clear();
    if (!LoadShaderFileIntoDesc(desc, errorText))
    {
        m_lastCreateError = errorText ? *errorText : std::string("reload: file read failed");
        return EffectHandle{meta->id, meta->generation};
    }

    if (desc.shaderSource.empty())
    {
        if (errorText)
            *errorText = "ReloadEffect: empty shader source";
        m_lastCreateError = *errorText;
        return EffectHandle{meta->id, meta->generation};
    }

    ImGuiRenderCore::ShaderDesc shaderDesc;
    shaderDesc.shaderKey = meta->shaderKey;
    shaderDesc.source = desc.shaderSource;
    shaderDesc.vertexEntry = desc.vertexEntry;
    shaderDesc.fragmentEntry = desc.fragmentEntry;

    const char* cErr = nullptr;
    if (!ImGui_ImplOpenGL3Slang_RegisterShaderProgram(shaderDesc, &cErr))
    {
        if (errorText)
        {
            if (cErr && cErr[0])
                *errorText = cErr;
            else if (const char* last = ImGui_ImplOpenGL3Slang_GetLastError(); last && last[0])
                *errorText = last;
            else
                *errorText = "RegisterShaderProgram failed during reload";
        }
        m_lastCreateError = (errorText && !errorText->empty()) ? *errorText : std::string("reload: compile failed");
        return EffectHandle{meta->id, meta->generation};
    }

    ImGuiRenderCore::PipelineDesc pipelineDesc;
    pipelineDesc.pipelineKey = meta->pipelineKey;
    pipelineDesc.shaderKey = meta->shaderKey;
    pipelineDesc.blendModeKey = BlendKey(desc.blendMode);
    ImGui_ImplOpenGL3Slang_RegisterPipeline(pipelineDesc);

    meta->storedDesc = desc;
    meta->storedDesc.shaderSource = desc.shaderSource;
    meta->generation++;
    UpdateShaderMtimeForMeta(*meta);
    m_lastCreateError.clear();
    return EffectHandle{meta->id, meta->generation};
}

void EffectSystem::ExpectEffectUniformBytes(EffectHandle handle, size_t byteCount)
{
    EffectMeta* meta = FindEffect(handle);
    if (!meta)
        return;
    meta->expectedUniformBytes = byteCount;
}

void EffectSystem::SetEffectPaletteTexture(EffectHandle handle, ImTextureID palette)
{
    EffectMeta* meta = FindEffect(handle);
    if (!meta)
        return;
    meta->paletteTexture = palette;
}

void EffectSystem::SetEffectUniformData(EffectHandle handle, const void* data, size_t bytes)
{
    EffectMeta* meta = FindEffect(handle);
    if (!meta || !data || bytes == 0)
        return;
    if (meta->expectedUniformBytes != 0 && bytes != meta->expectedUniformBytes)
        IM_ASSERT(false && "Effect uniform size mismatch (see ExpectEffectUniformBytes)");
    if (bytes > kEffectUniformBufferMaxBytes)
    {
        IM_ASSERT(false && "Effect uniform staging exceeds kEffectUniformBufferMaxBytes (see backend UBO size)");
        bytes = kEffectUniformBufferMaxBytes;
    }
    meta->effectUniformStaging.resize(bytes);
    std::memcpy(meta->effectUniformStaging.data(), data, bytes);
}

void EffectSystem::AdvanceFrame()
{
    m_frameIndex++;
    m_submitCountThisFrame = 0;
    m_framePhase = EffectFramePhase::AfterAdvance;
}

void EffectSystem::NotifyAfterImGuiNewFrame()
{
    m_framePhase = EffectFramePhase::AfterImGuiNewFrame;
}

void EffectSystem::TickAutoReload(float deltaTime)
{
    if (!m_autoReloadEnabled)
        return;
    m_autoReloadAccum += deltaTime;
    if (m_autoReloadAccum < m_autoReloadIntervalSec)
        return;
    m_autoReloadAccum = 0.f;

    std::vector<EffectHandle> reloadHandles;
    for (auto& kv : m_effects)
    {
        EffectMeta& em = kv.second;
        if (em.storedDesc.shaderFile.empty())
            continue;
        const long long tick = QueryFileMtimeTicks(em.storedDesc.shaderFile);
        if (tick == 0)
            continue;
        if (!em.hasShaderFileMtime)
        {
            em.shaderFileMtimeTick = tick;
            em.hasShaderFileMtime = true;
            continue;
        }
        if (tick != em.shaderFileMtimeTick)
            reloadHandles.push_back(EffectHandle{em.id, em.generation});
    }

    for (EffectHandle h : reloadHandles)
    {
        std::string err;
        ReloadEffect(h, &err);
        if (!err.empty())
            m_lastCreateError = err;
    }
}

bool EffectSystem::BeginEffectWindow(const char* name, EffectHandle effectHandle, bool* p_open, ImGuiWindowFlags flags, FontEffectPolicy fontPolicy)
{
    if (m_frameContractTracing && m_framePhase == EffectFramePhase::AfterAdvance)
        FrameContractTrace("BeginEffectWindow before NotifyAfterImGuiNewFrame(): call NotifyAfterImGuiNewFrame() after ImGui::NewFrame().");

    IM_ASSERT((int)m_openCaptures.size() < kMaxOpenEffectCaptureDepth && "Open effect captures exceed kMaxOpenEffectCaptureDepth (unbalanced Begin/End?)");

    TouchUiPhaseForCaptures();

    const bool open = ImGui::Begin(name, p_open, flags);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    OpenCapture cap;
    cap.kind = OpenCapture::Kind::Window;
    cap.token = effectHandle;
    cap.drawList = drawList;
    cap.viewportId = viewport ? viewport->ID : 0;
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 crMin = ImGui::GetWindowContentRegionMin();
    const ImVec2 crMax = ImGui::GetWindowContentRegionMax();
    cap.contentClipMin = ImVec2(windowPos.x + crMin.x, windowPos.y + crMin.y);
    cap.contentClipMax = ImVec2(windowPos.x + crMax.x, windowPos.y + crMax.y);
    cap.idxStart = drawList ? drawList->IdxBuffer.Size : 0;
    cap.idxEnd = cap.idxStart;
    cap.fontPolicy = fontPolicy;
    m_openCaptures.push_back(cap);
    return open;
}

void EffectSystem::EndEffectWindow()
{
    if (m_openCaptures.empty())
    {
        ImGui::End();
        return;
    }

    OpenCapture cap = m_openCaptures.back();
    m_openCaptures.pop_back();
    IM_ASSERT(cap.kind == OpenCapture::Kind::Window && "Mismatched EndEffectWindow (expected a window capture)");

    if (cap.drawList)
        cap.idxEnd = cap.drawList->IdxBuffer.Size;
    if (FindEffect(cap.token) != nullptr)
        m_queuedCaptures.push_back(cap);
    ImGui::End();
}

bool EffectSystem::BeginEffectDrawRegion(EffectHandle effectHandle, FontEffectPolicy fontPolicy)
{
    if (m_frameContractTracing && m_framePhase == EffectFramePhase::AfterAdvance)
        FrameContractTrace("BeginEffectDrawRegion before NotifyAfterImGuiNewFrame(): call NotifyAfterImGuiNewFrame() after ImGui::NewFrame().");

    IM_ASSERT((int)m_openCaptures.size() < kMaxOpenEffectCaptureDepth && "Open effect captures exceed kMaxOpenEffectCaptureDepth");

    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win == nullptr)
        return false;
    ImDrawList* drawList = win->DrawList;
    if (drawList == nullptr)
        return false;

    TouchUiPhaseForCaptures();

    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    OpenCapture cap;
    cap.kind = OpenCapture::Kind::Region;
    cap.token = effectHandle;
    cap.drawList = drawList;
    cap.viewportId = viewport ? viewport->ID : 0;
    const ImRect& ir = win->InnerClipRect;
    cap.contentClipMin = ir.Min;
    cap.contentClipMax = ir.Max;
    cap.idxStart = drawList->IdxBuffer.Size;
    cap.idxEnd = cap.idxStart;
    cap.fontPolicy = fontPolicy;
    m_openCaptures.push_back(cap);
    return true;
}

void EffectSystem::EndEffectDrawRegion()
{
    IM_ASSERT(!m_openCaptures.empty() && "EndEffectDrawRegion without BeginEffectDrawRegion");
    OpenCapture cap = m_openCaptures.back();
    m_openCaptures.pop_back();
    IM_ASSERT(cap.kind == OpenCapture::Kind::Region);

    if (cap.drawList)
        cap.idxEnd = cap.drawList->IdxBuffer.Size;
    if (FindEffect(cap.token) != nullptr)
        m_queuedCaptures.push_back(cap);
}

void EffectSystem::EnqueueCustomDraw(EffectHandle effectHandle, const ImGuiRenderCore::DrawPacket& packet)
{
    if (FindEffect(effectHandle) == nullptr)
        return;
    m_explicitPackets.push_back(std::make_pair(effectHandle, packet));
}

void EffectSystem::ProcessOneCapture(const OpenCapture& cap, const ImTextureID fontTexId, EffectDebugStats& ioStats)
{
    const EffectMeta* meta = FindEffect(cap.token);
    if (!meta)
    {
        ioStats.capturesSkippedStaleHandle++;
        return;
    }
    if (!cap.drawList)
        return;

    ioStats.capturesProcessed++;

    if (m_debugDrawCaptureClips)
    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        if (fg)
            fg->AddRect(cap.contentClipMin, cap.contentClipMax, IM_COL32(0, 255, 100, 200), 0.0f, 0, 2.0f);
    }

    const int currentIdxCount = cap.drawList->IdxBuffer.Size;
    const int idxStart = (cap.idxStart < 0) ? 0 : (cap.idxStart > currentIdxCount ? currentIdxCount : cap.idxStart);
    const int idxEnd = (cap.idxEnd < idxStart) ? idxStart : (cap.idxEnd > currentIdxCount ? currentIdxCount : cap.idxEnd);
    if (idxEnd <= idxStart)
        return;

    for (int i = 0; i < cap.drawList->CmdBuffer.Size; ++i)
    {
        const ImDrawCmd* cmd = &cap.drawList->CmdBuffer[i];
        if (cmd->UserCallback != nullptr || cmd->ElemCount == 0)
            continue;

        const int cmdIdxStart = (int)cmd->IdxOffset;
        const int cmdIdxEnd = (int)cmd->IdxOffset + (int)cmd->ElemCount;
        const int overlapStart = (idxStart > cmdIdxStart) ? idxStart : cmdIdxStart;
        const int overlapEnd = (idxEnd < cmdIdxEnd) ? idxEnd : cmdIdxEnd;
        if (overlapEnd <= overlapStart)
        {
            ioStats.drawCommandsSkippedNoOverlap++;
            continue;
        }

        const ImTextureID texId = cmd->TexRef._TexData ? cmd->TexRef._TexData->TexID : cmd->TexRef._TexID;
        if (texId == ImTextureID_Invalid)
        {
            ioStats.drawCommandsSkippedNoTexture++;
            continue;
        }
        if (cap.fontPolicy == FontEffectPolicy::SkipFontAtlas && fontTexId != ImTextureID_Invalid && texId == fontTexId)
        {
            ioStats.drawCommandsSkippedFont++;
            continue;
        }

        ImGuiRenderCore::DrawPacket packet;
        packet.pipelineKey = meta->pipelineKey;
        packet.texture = texId;
        packet.paletteTexture = meta->paletteTexture;
        packet.viewportId = cap.viewportId;
        packet.clipRect.x = (cmd->ClipRect.x > cap.contentClipMin.x) ? cmd->ClipRect.x : cap.contentClipMin.x;
        packet.clipRect.y = (cmd->ClipRect.y > cap.contentClipMin.y) ? cmd->ClipRect.y : cap.contentClipMin.y;
        packet.clipRect.z = (cmd->ClipRect.z < cap.contentClipMax.x) ? cmd->ClipRect.z : cap.contentClipMax.x;
        packet.clipRect.w = (cmd->ClipRect.w < cap.contentClipMax.y) ? cmd->ClipRect.w : cap.contentClipMax.y;
        if (packet.clipRect.z <= packet.clipRect.x || packet.clipRect.w <= packet.clipRect.y)
        {
            ioStats.drawCommandsSkippedZeroClip++;
            continue;
        }
        packet.elemCount = overlapEnd - overlapStart;
        packet.idxOffset = overlapStart;
        packet.vtxOffset = (int)cmd->VtxOffset;
        packet.drawList = cap.drawList;
        packet.drawCmd = nullptr;
        packet.isImGuiPacket = false;
        if (!meta->effectUniformStaging.empty() && meta->effectUniformBinding != 0)
        {
            if (meta->effectUniformStaging.size() > kEffectUniformBufferMaxBytes)
            {
                IM_ASSERT(false && "effectUniformStaging too large");
                continue;
            }
            packet.effectUniformBinding = meta->effectUniformBinding;
            packet.effectUniformBytes = meta->effectUniformStaging;
        }
        ImGui_ImplOpenGL3Slang_PushCustomDraw(meta->passKey.c_str(), packet);
        ioStats.drawCommandsSubmitted++;
    }
}

void EffectSystem::SubmitQueuedEffects()
{
    IM_ASSERT(m_openCaptures.empty() && "SubmitQueuedEffects: unmatched BeginEffectWindow/BeginEffectDrawRegion - call all End* first");
    if (m_submitCountThisFrame != 0)
        IM_ASSERT(false && "EffectSystem: SubmitQueuedEffects called twice this frame (call AdvanceFrame() at loop start; use a single EffectSubmitGuard or manual Submit)");
    m_submitCountThisFrame++;

    EffectDebugStats stats{};
    const ImTextureID fontTexId = ImGui::GetIO().Fonts ? ImGui::GetIO().Fonts->TexID.GetTexID() : ImTextureID_Invalid;

    for (const OpenCapture& cap : m_queuedCaptures)
        ProcessOneCapture(cap, fontTexId, stats);

    for (auto& ep : m_explicitPackets)
    {
        const EffectMeta* meta = FindEffect(ep.first);
        if (!meta)
            continue;
        ImGuiRenderCore::DrawPacket pkt = ep.second;
        if (pkt.pipelineKey.empty())
            pkt.pipelineKey = meta->pipelineKey;
        if (pkt.paletteTexture == ImTextureID_Invalid)
            pkt.paletteTexture = meta->paletteTexture;
        if (!meta->effectUniformStaging.empty() && meta->effectUniformBinding != 0)
        {
            if (meta->effectUniformStaging.size() > kEffectUniformBufferMaxBytes)
            {
                IM_ASSERT(false && "effectUniformStaging too large");
                continue;
            }
            pkt.effectUniformBinding = meta->effectUniformBinding;
            pkt.effectUniformBytes = meta->effectUniformStaging;
        }
        ImGui_ImplOpenGL3Slang_PushCustomDraw(meta->passKey.c_str(), pkt);
        stats.drawCommandsSubmitted++;
    }

    m_lastStats = stats;
    m_queuedCaptures.clear();
    m_explicitPackets.clear();
    m_framePhase = EffectFramePhase::AfterSubmit;
}

void EffectSystem::ClearQueuedEffects()
{
    m_openCaptures.clear();
    m_queuedCaptures.clear();
    m_explicitPackets.clear();
}

void EffectSystem::ShowDebugWindow(bool* p_open)
{
    if (!ImGui::Begin("Effect system (debug)", p_open))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Frame index: %llu  phase: %u", (unsigned long long)m_frameIndex, (unsigned)m_framePhase);
    ImGui::Checkbox("Trace frame contract (stderr)", &m_frameContractTracing);
    ImGui::TextUnformatted("Call AdvanceFrame() at loop start; NotifyAfterImGuiNewFrame() after ImGui::NewFrame().");
    ImGui::TextUnformatted("Use EffectSubmitGuard or a single SubmitQueuedEffects() before ImGui::Render().");
    ImGui::Separator();

    ImGui::Checkbox("Auto-reload shader files (shaderFile effects)", &m_autoReloadEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::DragFloat("interval (s)", &m_autoReloadIntervalSec, 0.05f, 0.1f, 5.f);
    ImGui::TextUnformatted("After successful reload, effect id is stable but generation increments (refresh FindEffectByName).");

    ImGui::Checkbox("Visualize capture clip rects", &m_debugDrawCaptureClips);
    ImGui::Separator();

    ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "Font policy: IncludeFontAtlas can corrupt window chrome / text.");
    if (ImGui::CollapsingHeader("Last submit stats", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Captures processed: %u", m_lastStats.capturesProcessed);
        ImGui::Text("Captures skipped (stale handle): %u", m_lastStats.capturesSkippedStaleHandle);
        ImGui::Text("Draw cmds submitted: %u", m_lastStats.drawCommandsSubmitted);
        ImGui::Text("Skipped (font atlas): %u", m_lastStats.drawCommandsSkippedFont);
        ImGui::Text("Skipped (no overlap): %u", m_lastStats.drawCommandsSkippedNoOverlap);
        ImGui::Text("Skipped (no texture): %u", m_lastStats.drawCommandsSkippedNoTexture);
        ImGui::Text("Skipped (zero clip): %u", m_lastStats.drawCommandsSkippedZeroClip);
    }
    if (ImGui::CollapsingHeader("Registered effects (named lookup)"))
    {
        for (const auto& kv : m_effectsByName)
        {
            auto mit = m_effects.find(kv.second);
            const unsigned gen = (mit != m_effects.end()) ? (unsigned)mit->second.generation : 0u;
            ImGui::BulletText("%s -> id %u gen %u", kv.first.c_str(), (unsigned)kv.second, gen);
        }
        if (m_effectsByName.empty())
            ImGui::TextUnformatted("(no named effects)");
    }
    if (ImGui::CollapsingHeader("Effect details"))
    {
        for (const auto& kv : m_effects)
        {
            const EffectMeta& m = kv.second;
            ImGui::PushID((int)m.id);
            ImGui::BulletText("%s (id %u gen %u)", m.name.c_str(), (unsigned)m.id, (unsigned)m.generation);
            ImGui::Indent();
            if (!m.registeredLookupName.empty())
                ImGui::Text("lookup: %s", m.registeredLookupName.c_str());
            ImGui::TextWrapped("shaderKey: %s", m.shaderKey.c_str());
            ImGui::TextWrapped("pipeline: %s", m.pipelineKey.c_str());
            if (!m.storedDesc.shaderFile.empty())
                ImGui::TextWrapped("shaderFile: %s", m.storedDesc.shaderFile.c_str());
            ImGui::Text("uniform bytes staged: %zu expected: %zu", m.effectUniformStaging.size(), m.expectedUniformBytes);
            ImGui::Unindent();
            ImGui::PopID();
        }
        if (m_effects.empty())
            ImGui::TextUnformatted("(none)");
        static int s_reloadId = 1;
        ImGui::InputInt("Reload effect id", &s_reloadId);
        if (ImGui::Button("Reload by id"))
        {
            auto it = m_effects.find((uint32_t)s_reloadId);
            std::string err;
            if (it == m_effects.end())
            {
                err = "No effect with this id";
                ImGui::SetClipboardText(err.c_str());
                ImGui::TextUnformatted("Failed - error on clipboard");
            }
            else
            {
                EffectHandle h{it->second.id, it->second.generation};
                EffectHandle nu = ReloadEffect(h, &err);
                const std::string clip = err.empty()
                    ? ("id=" + std::to_string(nu.id) + " gen=" + std::to_string(nu.generation))
                    : err;
                ImGui::SetClipboardText(clip.c_str());
                if (err.empty())
                    ImGui::Text("OK - token on clipboard: id %u gen %u", (unsigned)nu.id, (unsigned)nu.generation);
                else
                    ImGui::TextUnformatted("Failed - error on clipboard");
            }
        }
    }

    if (!m_lastCreateError.empty())
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Last CreateEffect / reload error:");
        ImGui::BeginChild("##lastcreate", ImVec2(0, ImGui::GetTextLineHeight() * 8), ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted(m_lastCreateError.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }
    if (const char* last = ImGui_ImplOpenGL3Slang_GetLastError(); last && last[0])
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Last ImGui_ImplOpenGL3Slang_GetLastError():");
        ImGui::TextWrapped("%s", last);
    }
    ImGui::End();
}

} // namespace ImGuiRenderUX
