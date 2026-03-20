#pragma once

#include "imgui.h"
#include "imgui_impl_opengl3_slang.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ImGuiRenderUX
{
/// Max bytes uploaded per draw for `effectUniformBytes` / per-effect UBO scratch buffer (see OpenGL backend).
inline constexpr std::size_t kEffectUniformBufferMaxBytes = 256;

enum class BuiltinBlendMode
{
    Alpha,
    Additive,
    Multiply,
    PremultipliedAlpha,
};

/// Controls whether draw commands that use the font atlas are sent through the effect pass.
enum class FontEffectPolicy
{
    SkipFontAtlas,    // Default: avoids corrupting window chrome / glyphs in common cases.
    IncludeFontAtlas, // Post-process text draws too (may cause artifacts; use when intentional).
};

using EffectHandle = uint32_t;
static constexpr EffectHandle kInvalidEffectHandle = 0;

struct EffectCreateDesc
{
    std::string name;
    std::string shaderSource;
    std::string shaderFile;
    std::string vertexEntry = "vertexMain";
    std::string fragmentEntry = "fragmentMain";
    BuiltinBlendMode blendMode = BuiltinBlendMode::Alpha;
    std::string passKey = "OverlayPass";
    FontEffectPolicy fontPolicy = FontEffectPolicy::SkipFontAtlas;
    uint32_t effectUniformBinding = 2;
};

struct EffectDebugStats
{
    uint32_t drawCommandsSubmitted = 0;
    uint32_t drawCommandsSkippedFont = 0;
    uint32_t drawCommandsSkippedNoOverlap = 0;
    uint32_t drawCommandsSkippedNoTexture = 0;
    uint32_t drawCommandsSkippedZeroClip = 0;
    uint32_t capturesProcessed = 0;
};

/// Optional: one shared 1x1 white RGBA texture for tinted `AddImage` quads (OpenGL; call after GL loader + context).
class BuiltinGpuTextures
{
public:
    void EnsureWhite1x1();
    void Destroy();
    ImTextureID White1x1() const { return m_whiteImgui; }
    unsigned    GlTexture() const { return m_glTex; }

private:
    unsigned    m_glTex = 0;
    ImTextureID m_whiteImgui{};
};

class EffectSystem
{
public:
    bool Initialize();

    EffectHandle CreateEffect(const EffectCreateDesc& desc, std::string* errorText = nullptr);
    EffectHandle CreateEffectFromFile(const EffectCreateDesc& desc, std::string* errorText = nullptr);

    /// If `desc.name` is non-empty and already registered, returns the existing handle (no recompile).
    EffectHandle EnsureEffect(const EffectCreateDesc& desc, std::string* errorText = nullptr);

    EffectHandle FindEffectByName(const std::string& name) const;

    bool DestroyEffect(EffectHandle handle);

    EffectHandle ReloadEffect(EffectHandle handle, std::string* errorText = nullptr);

    void SetEffectUniformData(EffectHandle handle, const void* data, size_t bytes);

    template<typename T>
    void SetEffectUniformStruct(EffectHandle handle, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "Effect UBO struct must be trivially copyable");
        static_assert(sizeof(T) <= kEffectUniformBufferMaxBytes, "Effect UBO struct exceeds kEffectUniformBufferMaxBytes");
        SetEffectUniformData(handle, &value, sizeof(T));
    }

    bool BeginEffectWindow(const char* name, EffectHandle effectHandle, bool* p_open = nullptr, ImGuiWindowFlags flags = 0, FontEffectPolicy fontPolicy = FontEffectPolicy::SkipFontAtlas);
    void EndEffectWindow();

    bool BeginEffectDrawRegion(EffectHandle effectHandle, FontEffectPolicy fontPolicy = FontEffectPolicy::SkipFontAtlas);
    void EndEffectDrawRegion();

    void EnqueueCustomDraw(EffectHandle effectHandle, const ImGuiRenderCore::DrawPacket& packet);

    /// Call once at the beginning of each application frame (e.g. top of the main loop) to detect double Submit.
    void AdvanceFrame();

    void SubmitQueuedEffects();
    void ClearQueuedEffects();

    EffectDebugStats GetLastSubmitStats() const { return m_lastStats; }

    void ShowDebugWindow(bool* p_open);

    void SetDebugDrawCaptureClips(bool enable) { m_debugDrawCaptureClips = enable; }
    bool GetDebugDrawCaptureClips() const { return m_debugDrawCaptureClips; }

    /// Poll shader files on disk (requires `ImGui::GetTime()`, call after `NewFrame`). Updates named effects via `ReloadEffect`.
    void TickAutoReload(float deltaTime);

    void SetAutoReloadShaders(bool enable) { m_autoReloadEnabled = enable; }
    bool GetAutoReloadShaders() const { return m_autoReloadEnabled; }
    void SetAutoReloadInterval(float seconds) { m_autoReloadIntervalSec = seconds; }

    const std::string& GetLastCreateError() const { return m_lastCreateError; }

private:
    struct EffectMeta
    {
        EffectHandle handle = kInvalidEffectHandle;
        std::string name;
        std::string registeredLookupName;
        std::string shaderKey;
        std::string pipelineKey;
        std::string passKey;
        EffectCreateDesc storedDesc;
        uint32_t effectUniformBinding = 2;
        std::vector<uint8_t> effectUniformStaging;
        long long    shaderFileMtimeTick = 0;
        bool         hasShaderFileMtime = false;
    };

    struct OpenCapture
    {
        enum class Kind : uint8_t { Window, Region } kind;
        EffectHandle effectHandle = kInvalidEffectHandle;
        ImDrawList*  drawList = nullptr;
        ImGuiID      viewportId = 0;
        ImVec2       contentClipMin{};
        ImVec2       contentClipMax{};
        int          idxStart = 0;
        int          idxEnd = 0;
        FontEffectPolicy fontPolicy = FontEffectPolicy::SkipFontAtlas;
    };

    static bool        ReadTextFile(const std::string& path, std::string& outText);
    static const char* BlendKey(BuiltinBlendMode mode);
    static ImGuiRenderCore::BlendStateDesc BlendDesc(BuiltinBlendMode mode);
    static long long   QueryFileMtimeTicks(const std::string& path);

    EffectMeta*       FindEffect(EffectHandle handle);
    const EffectMeta* FindEffect(EffectHandle handle) const;

    void ProcessOneCapture(const OpenCapture& cap, ImTextureID fontTexId, EffectDebugStats& ioStats);
    void RegisterEffectName(const std::string& lookupName, EffectHandle handle);
    void UnregisterEffectName(const std::string& lookupName);
    void UpdateShaderMtimeForMeta(EffectMeta& meta);

    bool     m_initialized = false;
    EffectHandle m_nextHandle = 1;
    std::unordered_map<EffectHandle, EffectMeta> m_effects;
    std::unordered_map<std::string, EffectHandle> m_effectsByName;
    std::vector<OpenCapture> m_openCaptures;
    std::vector<OpenCapture> m_queuedCaptures;
    std::vector<std::pair<EffectHandle, ImGuiRenderCore::DrawPacket>> m_explicitPackets;
    EffectDebugStats m_lastStats{};
    bool             m_debugDrawCaptureClips = false;

    uint64_t  m_frameIndex = 0;
    uint32_t  m_submitCountThisFrame = 0;
    std::string m_lastCreateError;

    bool      m_autoReloadEnabled = false;
    float     m_autoReloadIntervalSec = 0.25f;
    float     m_autoReloadAccum = 0.f;
};

/// Destructor calls `SubmitQueuedEffects()` — use instead of a manual submit at end of UI (before `ImGui::Render`).
struct EffectSubmitGuard
{
    EffectSystem& System;
    bool Cancelled = false;
    explicit EffectSubmitGuard(EffectSystem& system) : System(system) {}
    void Cancel() { Cancelled = true; }
    ~EffectSubmitGuard()
    {
        if (!Cancelled)
            System.SubmitQueuedEffects();
    }
};

struct EffectWindowScope
{
    EffectSystem& System;
    bool Open = false;
    EffectWindowScope(EffectSystem& system, const char* name, EffectHandle effect, bool* p_open = nullptr, ImGuiWindowFlags flags = 0, FontEffectPolicy fontPolicy = FontEffectPolicy::SkipFontAtlas)
        : System(system), Open(system.BeginEffectWindow(name, effect, p_open, flags, fontPolicy)) {}
    ~EffectWindowScope() { System.EndEffectWindow(); }
    explicit operator bool() const { return Open; }
};

struct EffectDrawRegionScope
{
    EffectSystem& System;
    bool Active = false;
    EffectDrawRegionScope(EffectSystem& system, EffectHandle effect, FontEffectPolicy fontPolicy = FontEffectPolicy::SkipFontAtlas)
        : System(system)
    {
        Active = system.BeginEffectDrawRegion(effect, fontPolicy);
    }
    ~EffectDrawRegionScope()
    {
        if (Active)
            System.EndEffectDrawRegion();
    }
    explicit operator bool() const { return Active; }
};

} // namespace ImGuiRenderUX
