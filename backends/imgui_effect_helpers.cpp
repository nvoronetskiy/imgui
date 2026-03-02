#include "imgui_effect_helpers.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ImGuiRenderUX
{
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
        // defaults are already alpha blending
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
            return kInvalidEffectHandle;
        }
        resolved.shaderFile = loadedFrom;
    }
    return CreateEffect(resolved, errorText);
}

EffectHandle EffectSystem::CreateEffect(const EffectCreateDesc& desc, std::string* errorText)
{
    if (!m_initialized)
        Initialize();

    if (desc.shaderSource.empty())
    {
        if (errorText)
            *errorText = "Effect shaderSource is empty";
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
            *errorText = cErr ? cErr : "RegisterShaderProgram failed";
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
    m_effects[handle] = std::move(meta);
    return handle;
}

bool EffectSystem::BeginEffectWindow(const char* name, EffectHandle effectHandle, bool* p_open, ImGuiWindowFlags flags)
{
    const bool open = ImGui::Begin(name, p_open, flags);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    WindowCapture cap;
    cap.effectHandle = effectHandle;
    cap.drawList = drawList;
    cap.viewportId = viewport ? viewport->ID : 0;
    // Capture by index-buffer range because ImGui may append to an existing ImDrawCmd
    // instead of creating a new command object.
    cap.idxStart = drawList ? drawList->IdxBuffer.Size : 0;
    cap.idxEnd = cap.idxStart;
    m_activeWindowStack.push_back(cap);
    return open;
}

void EffectSystem::EndEffectWindow()
{
    if (m_activeWindowStack.empty())
    {
        ImGui::End();
        return;
    }

    WindowCapture cap = m_activeWindowStack.back();
    m_activeWindowStack.pop_back();

    if (cap.drawList)
        cap.idxEnd = cap.drawList->IdxBuffer.Size;
    if (FindEffect(cap.effectHandle) != nullptr)
        m_queuedCaptures.push_back(cap);
    ImGui::End();
}

void EffectSystem::SubmitQueuedEffects()
{
    for (const WindowCapture& cap : m_queuedCaptures)
    {
        const EffectMeta* meta = FindEffect(cap.effectHandle);
        if (!meta || !cap.drawList)
            continue;

        const int currentIdxCount = cap.drawList->IdxBuffer.Size;
        const int idxStart = (cap.idxStart < 0) ? 0 : (cap.idxStart > currentIdxCount ? currentIdxCount : cap.idxStart);
        const int idxEnd = (cap.idxEnd < idxStart) ? idxStart : (cap.idxEnd > currentIdxCount ? currentIdxCount : cap.idxEnd);
        if (idxEnd <= idxStart)
            continue;

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
                continue;

            // Avoid ImDrawCmd::GetTexID() assert when backend-managed textures are not uploaded yet.
            const ImTextureID texId = cmd->TexRef._TexData ? cmd->TexRef._TexData->TexID : cmd->TexRef._TexID;
            if (texId == ImTextureID_Invalid)
                continue;

            ImGuiRenderCore::DrawPacket packet;
            packet.pipelineKey = meta->pipelineKey;
            packet.texture = texId;
            packet.viewportId = 0; // route custom packets by current draw-list membership, not transient viewport migration
            packet.clipRect = cmd->ClipRect;
            packet.elemCount = overlapEnd - overlapStart;
            packet.idxOffset = overlapStart;
            packet.vtxOffset = (int)cmd->VtxOffset;
            packet.drawList = cap.drawList;
            packet.drawCmd = nullptr; // custom pass must not depend on transient ImDrawCmd pointer
            packet.isImGuiPacket = false;
            ImGui_ImplOpenGL3Slang_PushCustomDraw(meta->passKey.c_str(), packet);
        }
    }
    m_queuedCaptures.clear();
}

void EffectSystem::ClearQueuedEffects()
{
    m_activeWindowStack.clear();
    m_queuedCaptures.clear();
}
} // namespace ImGuiRenderUX
