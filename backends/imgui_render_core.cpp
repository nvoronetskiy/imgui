#include "imgui_render_core.h"

namespace ImGuiRenderCore
{
void FrameCommandList::Clear()
{
    packets.clear();
    uniformUpdates.clear();
}

void FrameCommandList::PushPacket(const DrawPacket& packet)
{
    packets.push_back(packet);
}

void FrameCommandList::PushUniformUpdate(const UniformBlockUpdate& update)
{
    uniformUpdates.push_back(update);
}

void CustomPassRegistry::PushPacket(const std::string& passKey, const DrawPacket& packet)
{
    auto& pass = m_passes[passKey];
    pass.passKey = passKey;
    pass.packets.push_back(packet);
}

std::vector<CustomPassData> CustomPassRegistry::ConsumePasses()
{
    std::vector<CustomPassData> out;
    out.reserve(m_passes.size());
    for (auto& it : m_passes)
        out.push_back(std::move(it.second));
    m_passes.clear();
    return out;
}

void CustomPassRegistry::Clear()
{
    m_passes.clear();
}
} // namespace ImGuiRenderCore
