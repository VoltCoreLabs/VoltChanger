#pragma once
#include <cstdint>
class Panel;
namespace MetaPanel
{
    void Toggle(std::intptr_t Hud);
    void Update();
    std::uint64_t PlayerViewRevision();
    void RenderPlayerAnalysis(const Panel& Parent);
}
