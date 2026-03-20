#include "imgui_effect_helpers.h"
#include "imgui_internal.h"

#ifndef IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif
#include <GL/gl3w.h>

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
} // namespace

void BuiltinGpuTextures::EnsureWhite1x1()
{
    if (m_glTex != 0)
        return;
    unsigned char white[] = { 255, 255, 255, 255 };
    glGenTextures(1, &m_glTex);
    glBindTexture(GL_TEXTURE_2D, m_glTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_whiteImgui = (ImTextureID)(intptr_t)m_glTex;
}

void BuiltinGpuTextures::Destroy()
{
    if (m_glTex != 0)
    {
        glDeleteTextures(1, &m_glTex);
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
    auto it = m_effects.find(handle);
    if (it == m_effects.end())
        return nullptr;
    return &it->second;
}

const EffectSystem::EffectMeta* EffectSystem::FindEffect(EffectHandle handle) const
{
    auto it = m_effects.find(handle);
    if (it == m_effects.end())
        return nullptr;
    return &it->second;
}

EffectHandle EffectSystem::FindEffectByName(const std::string& name) const
{
    auto it = m_effectsByName.find(name);
    if (it == m_effectsByName.end())
        return kInvalidEffectHandle;
    return it->second;
}

void EffectSystem::RegisterEffectName(const std::string& lookupName, EffectHandle handle)
{
    if (lookupName.empty())
        return;
    m_effectsByName[lookupName] = handle;
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
            return it->second;
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
    EffectCreateDesc resolved = desc;
    if (resolved.shaderSource.empty() && !resolved.shaderFile.empty())
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates;
        const fs::path rawPath(resolved.shaderFile);
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
            if (ReadTextFile(tryPath, resolved.shaderSource))
            {
                loaded = true;
                loadedFrom = tryPath;
                break;
            }
        }

        if (!loaded)
        {
            if (errorText)
                *errorText = "Failed to read shader file: " + resolved.shaderFile + " (cwd: " + fs::current_path().string() + ")";
            m_lastCreateError = errorText ? *errorText : "Failed to read shader file";
            return kInvalidEffectHandle;
        }
        resolved.shaderFile = loadedFrom;
    }
    EffectHandle h = CreateEffect(resolved, errorText);
    if (h != kInvalidEffectHandle)
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
            if (FindEffect(itName->second) == nullptr)
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

    EffectHandle handle = m_nextHandle++;
    const std::string baseName = desc.name.empty() ? ("effect_" + std::to_string(handle)) : desc.name;
    const std::string shaderKey = "ux_shader_" + baseName + "_" + std::to_string(handle);
    const std::string pipelineKey = "ux_pipeline_" + baseName + "_" + std::to_string(handle);
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
    meta.handle = handle;
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
        RegisterEffectName(desc.name, handle);
    }
    UpdateShaderMtimeForMeta(meta);

    m_effects[handle] = std::move(meta);
    return handle;
}

bool EffectSystem::DestroyEffect(EffectHandle handle)
{
    auto it = m_effects.find(handle);
    if (it == m_effects.end())
        return false;
    UnregisterEffectName(it->second.registeredLookupName);
    ImGui_ImplOpenGL3Slang_UnregisterEffectResources(it->second.shaderKey.c_str(), it->second.pipelineKey.c_str());
    m_effects.erase(it);
    return true;
}

EffectHandle EffectSystem::ReloadEffect(EffectHandle handle, std::string* errorText)
{
    auto it = m_effects.find(handle);
    if (it == m_effects.end())
        return kInvalidEffectHandle;
    EffectCreateDesc desc = it->second.storedDesc;
    DestroyEffect(handle);
    if (!desc.shaderFile.empty())
    {
        desc.shaderSource.clear();
        return CreateEffectFromFile(desc, errorText);
    }
    return CreateEffect(desc, errorText);
}

void EffectSystem::SetEffectUniformData(EffectHandle handle, const void* data, size_t bytes)
{
    EffectMeta* meta = FindEffect(handle);
    if (!meta || !data || bytes == 0)
        return;
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
        EffectMeta& meta = kv.second;
        if (meta.storedDesc.shaderFile.empty())
            continue;
        const long long tick = QueryFileMtimeTicks(meta.storedDesc.shaderFile);
        if (tick == 0)
            continue;
        if (!meta.hasShaderFileMtime)
        {
            meta.shaderFileMtimeTick = tick;
            meta.hasShaderFileMtime = true;
            continue;
        }
        if (tick != meta.shaderFileMtimeTick)
            reloadHandles.push_back(kv.first);
    }

    for (EffectHandle h : reloadHandles)
    {
        std::string err;
        EffectHandle nu = ReloadEffect(h, &err);
        if (nu == kInvalidEffectHandle && !err.empty())
            m_lastCreateError = err;
    }
}

bool EffectSystem::BeginEffectWindow(const char* name, EffectHandle effectHandle, bool* p_open, ImGuiWindowFlags flags, FontEffectPolicy fontPolicy)
{
    const bool open = ImGui::Begin(name, p_open, flags);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    OpenCapture cap;
    cap.kind = OpenCapture::Kind::Window;
    cap.effectHandle = effectHandle;
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
    if (FindEffect(cap.effectHandle) != nullptr)
        m_queuedCaptures.push_back(cap);
    ImGui::End();
}

bool EffectSystem::BeginEffectDrawRegion(EffectHandle effectHandle, FontEffectPolicy fontPolicy)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win == nullptr)
        return false;
    ImDrawList* drawList = win->DrawList;
    if (drawList == nullptr)
        return false;
    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    OpenCapture cap;
    cap.kind = OpenCapture::Kind::Region;
    cap.effectHandle = effectHandle;
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
    if (FindEffect(cap.effectHandle) != nullptr)
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
    const EffectMeta* meta = FindEffect(cap.effectHandle);
    if (!meta || !cap.drawList)
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

    ImGui::Text("Frame index: %llu", (unsigned long long)m_frameIndex);
    ImGui::TextUnformatted("Call AdvanceFrame() once at the start of each app loop iteration.");
    ImGui::TextUnformatted("Use EffectSubmitGuard or a single SubmitQueuedEffects() before ImGui::Render().");
    ImGui::Separator();

    ImGui::Checkbox("Auto-reload shader files (shaderFile effects)", &m_autoReloadEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::DragFloat("interval (s)", &m_autoReloadIntervalSec, 0.05f, 0.1f, 5.f);
    ImGui::TextUnformatted("After reload, use FindEffectByName(\"...\") - new GPU handle.");

    ImGui::Checkbox("Visualize capture clip rects", &m_debugDrawCaptureClips);
    ImGui::Separator();

    ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "Font policy: IncludeFontAtlas can corrupt window chrome / text.");
    if (ImGui::CollapsingHeader("Last submit stats", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Captures: %u", m_lastStats.capturesProcessed);
        ImGui::Text("Draw cmds submitted: %u", m_lastStats.drawCommandsSubmitted);
        ImGui::Text("Skipped (font atlas): %u", m_lastStats.drawCommandsSkippedFont);
        ImGui::Text("Skipped (no overlap): %u", m_lastStats.drawCommandsSkippedNoOverlap);
        ImGui::Text("Skipped (no texture): %u", m_lastStats.drawCommandsSkippedNoTexture);
        ImGui::Text("Skipped (zero clip): %u", m_lastStats.drawCommandsSkippedZeroClip);
    }
    if (ImGui::CollapsingHeader("Registered effects (named lookup)"))
    {
        for (const auto& kv : m_effectsByName)
            ImGui::BulletText("%s -> handle %u", kv.first.c_str(), (unsigned)kv.second);
        if (m_effectsByName.empty())
            ImGui::TextUnformatted("(no named effects)");
    }
    if (ImGui::CollapsingHeader("Effect details"))
    {
        for (const auto& kv : m_effects)
        {
            const EffectMeta& m = kv.second;
            ImGui::PushID((int)m.handle);
            ImGui::BulletText("%s (handle %u)", m.name.c_str(), (unsigned)m.handle);
            ImGui::Indent();
            if (!m.registeredLookupName.empty())
                ImGui::Text("lookup: %s", m.registeredLookupName.c_str());
            ImGui::TextWrapped("shaderKey: %s", m.shaderKey.c_str());
            ImGui::TextWrapped("pipeline: %s", m.pipelineKey.c_str());
            if (!m.storedDesc.shaderFile.empty())
                ImGui::TextWrapped("shaderFile: %s", m.storedDesc.shaderFile.c_str());
            ImGui::Text("uniform bytes staged: %zu", m.effectUniformStaging.size());
            ImGui::Unindent();
            ImGui::PopID();
        }
        if (m_effects.empty())
            ImGui::TextUnformatted("(none)");
        static int s_reloadHandle = 1;
        ImGui::InputInt("Reload effect handle", &s_reloadHandle);
        if (ImGui::Button("Reload by handle"))
        {
            std::string err;
            EffectHandle nu = ReloadEffect((EffectHandle)(unsigned)s_reloadHandle, &err);
            if (nu != kInvalidEffectHandle)
            {
                ImGui::SetClipboardText(std::to_string((unsigned)nu).c_str());
                ImGui::Text("OK - new handle on clipboard: %u", (unsigned)nu);
            }
            else
            {
                ImGui::SetClipboardText(err.c_str());
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
