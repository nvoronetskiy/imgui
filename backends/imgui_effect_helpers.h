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

/// Max nested BeginEffectWindow / BeginEffectDrawRegion without matching End* (per frame).
inline constexpr int kMaxOpenEffectCaptureDepth = 64;

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

/// Stable effect id + generation: after a successful hot-reload, `generation` increments; older tokens become invalid.
struct EffectHandle
{
    uint32_t id = 0;
    uint32_t generation = 0;

    constexpr bool IsValid() const noexcept { return id != 0u; }
    friend constexpr bool operator==(EffectHandle a, EffectHandle b) noexcept
    {
        return a.id == b.id && a.generation == b.generation;
    }
    friend constexpr bool operator!=(EffectHandle a, EffectHandle b) noexcept { return !(a == b); }
};

inline constexpr EffectHandle kInvalidEffectHandle{};

/// Optional: document / static_assert that a C++ struct mirrors your Slang `cbuffer` (binding 2, std140).
#define IMGUI_EFFECT_UNIFORM_STRUCT(TypeName) \
    static_assert(sizeof(TypeName) <= ::ImGuiRenderUX::kEffectUniformBufferMaxBytes, \
                  #TypeName " exceeds kEffectUniformBufferMaxBytes")

/// Frame contract (call order). Use with `SetEffectFrameContractTracing(true)` to log violations (stderr).
enum class EffectFramePhase : uint8_t
{
    None = 0,
    AfterAdvance,          // After `AdvanceFrame()`, before `ImGui::NewFrame()`
    AfterImGuiNewFrame,    // After `NotifyAfterImGuiNewFrame()` / `ImGui::NewFrame()`
    UiCapturing,           // At least one effect capture is open
    AfterSubmit,           // After `SubmitQueuedEffects()` this frame
};

/// Override OpenGL entry points for `BuiltinGpuTextures` (no gl3w in your TU). GLenum values match `unsigned`.
/// Leave null to use bundled gl3w defaults (after `IMGUI_IMPL_OPENGL_LOADER_CUSTOM` + include).
struct EffectTextureGLProcs
{
    void (*GenTextures)(int n, unsigned* textures);
    void (*DeleteTextures)(int n, const unsigned* textures);
    void (*BindTexture)(unsigned target, unsigned texture);
    void (*TexParameteri)(unsigned target, unsigned pname, int param);
    void (*TexImage2D)(unsigned target, int level, int internalformat, int width, int height, int border,
                       unsigned format, unsigned type, const void* pixels);
};

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
    uint32_t capturesSkippedStaleHandle = 0;
};

/// Optional: one shared 1x1 white RGBA texture for tinted `AddImage` quads (OpenGL; call after GL loader + context).
class BuiltinGpuTextures
{
public:
    /// If non-null, all subsequent EnsureWhite1x1 / Destroy use these procs. Pass nullptr to restore gl3w defaults.
    static void SetTextureProcs(const EffectTextureGLProcs* procs);

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

    /// If `desc.name` is non-empty and already registered, returns the current handle token (no recompile).
    EffectHandle EnsureEffect(const EffectCreateDesc& desc, std::string* errorText = nullptr);

    EffectHandle FindEffectByName(const std::string& name) const;

    /// `handle.generation` must match the live effect, otherwise returns false (stale token).
    bool DestroyEffect(EffectHandle handle);

    /// Recompiles in place (stable `id`). On success, `generation` increments (invalidate old tokens).
    /// On failure, returns the same token as input and fills `errorText`; previous GPU program remains.
    EffectHandle ReloadEffect(EffectHandle handle, std::string* errorText = nullptr);

    void SetEffectUniformData(EffectHandle handle, const void* data, size_t bytes);

    /// If non-zero, `SetEffectUniformData` must receive exactly this many bytes (debug builds assert).
    void ExpectEffectUniformBytes(EffectHandle handle, size_t byteCount);

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

    void EnqueueCustomDraw(EffectHandle handle, const ImGuiRenderCore::DrawPacket& packet);

    /// Call once at the beginning of each application frame (e.g. top of the main loop).
    void AdvanceFrame();

    /// Call once after `ImGui::NewFrame()` (before UI that uses effect captures). Enables frame-contract checks.
    void NotifyAfterImGuiNewFrame();

    void SubmitQueuedEffects();
    void ClearQueuedEffects();

    EffectDebugStats GetLastSubmitStats() const { return m_lastStats; }

    void ShowDebugWindow(bool* p_open);

    void SetDebugDrawCaptureClips(bool enable) { m_debugDrawCaptureClips = enable; }
    bool GetDebugDrawCaptureClips() const { return m_debugDrawCaptureClips; }

    /// Log frame-order problems to stderr (missing NotifyAfterImGuiNewFrame, max capture depth, etc.).
    void SetEffectFrameContractTracing(bool enable) { m_frameContractTracing = enable; }
    bool GetEffectFrameContractTracing() const { return m_frameContractTracing; }
    EffectFramePhase GetEffectFramePhase() const { return m_framePhase; }

    bool IsEffectHandleLive(EffectHandle handle) const;

    void TickAutoReload(float deltaTime);

    void SetAutoReloadShaders(bool enable) { m_autoReloadEnabled = enable; }
    bool GetAutoReloadShaders() const { return m_autoReloadEnabled; }
    void SetAutoReloadInterval(float seconds) { m_autoReloadIntervalSec = seconds; }

    const std::string& GetLastCreateError() const { return m_lastCreateError; }

private:
    struct EffectMeta
    {
        uint32_t     id = 0;
        uint32_t     generation = 1;
        std::string  name;
        std::string  registeredLookupName;
        std::string  shaderKey;
        std::string  pipelineKey;
        std::string  passKey;
        EffectCreateDesc storedDesc;
        uint32_t effectUniformBinding = 2;
        std::vector<uint8_t> effectUniformStaging;
        size_t   expectedUniformBytes = 0;
        long long    shaderFileMtimeTick = 0;
        bool         hasShaderFileMtime = false;
    };

    struct OpenCapture
    {
        enum class Kind : uint8_t { Window, Region } kind;
        EffectHandle token{};
        ImDrawList*  drawList = nullptr;
        ImGuiID      viewportId = 0;
        ImVec2       contentClipMin{};
        ImVec2       contentClipMax{};
        int          idxStart = 0;
        int          idxEnd = 0;
        FontEffectPolicy fontPolicy = FontEffectPolicy::SkipFontAtlas;
    };

    static bool        ReadTextFile(const std::string& path, std::string& outText);
    static bool        LoadShaderFileIntoDesc(EffectCreateDesc& ioDesc, std::string* errorText);
    static const char* BlendKey(BuiltinBlendMode mode);
    static ImGuiRenderCore::BlendStateDesc BlendDesc(BuiltinBlendMode mode);
    static long long   QueryFileMtimeTicks(const std::string& path);

    void FrameContractTrace(const char* message) const;
    void TouchUiPhaseForCaptures();

    EffectMeta*       FindEffect(EffectHandle handle);
    const EffectMeta* FindEffect(EffectHandle handle) const;

    void ProcessOneCapture(const OpenCapture& cap, ImTextureID fontTexId, EffectDebugStats& ioStats);
    void RegisterEffectName(const std::string& lookupName, uint32_t id);
    void UnregisterEffectName(const std::string& lookupName);
    void UpdateShaderMtimeForMeta(EffectMeta& meta);

    bool     m_initialized = false;
    uint32_t m_nextId = 1;
    std::unordered_map<uint32_t, EffectMeta> m_effects;
    std::unordered_map<std::string, uint32_t> m_effectsByName;
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

    bool               m_frameContractTracing = false;
    EffectFramePhase   m_framePhase = EffectFramePhase::None;
    bool               m_warnedMissingNewFrame = false;
};

/// Destructor calls `SubmitQueuedEffects()` - use instead of a manual submit at end of UI (before `ImGui::Render`).
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
