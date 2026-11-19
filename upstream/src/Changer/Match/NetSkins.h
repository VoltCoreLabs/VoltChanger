#pragma once

#include <cstdint>
namespace NetDelta { class PacketView; struct EntityState; struct EntityEdits; }

namespace NetSkins
{
    void Update();
    int LocalHeroIndex();
    bool IsSwapped(int Index, std::uint64_t ModelId);
    bool IsOwnHero(int Index);
    std::uint64_t DefaultModelOf(int Index);
    void TranslateReplacement(const NetDelta::PacketView& View, int Index, const NetDelta::EntityState& Entity,
                              std::uint64_t From, std::uint64_t To, NetDelta::EntityEdits& Edits);
    void RecordReplacement(int Index, std::uint64_t Model, std::uint64_t ServerModel);
}
