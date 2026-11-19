#pragma once

#include <cstdint>

class C_EconItemView;
class CDOTAPlayerInventory;

namespace LoadoutOverride
{
    // Current Dota uses class 1000 for the account loadout. Also accept the
    // legacy class 0. Inventory ownership is checked separately in Apply.
    constexpr bool AcceptsClass(std::uint32_t LocalHeroId, std::uint32_t RequestedClass)
    {
        return !LocalHeroId || !RequestedClass || RequestedClass == 1000 || RequestedClass == LocalHeroId;
    }
    C_EconItemView* Apply(CDOTAPlayerInventory* Inventory, std::uint32_t HeroId, std::uint32_t Slot, C_EconItemView* Current);
    C_EconItemView* SelectedTerrainView();
}
