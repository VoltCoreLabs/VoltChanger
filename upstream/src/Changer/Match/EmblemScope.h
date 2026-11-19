#pragma once
#include <cstdint>
#include <cstring>

namespace EmblemScope
{
    struct LoadoutEntry
    {
        std::uintptr_t View = 0;
        std::uint32_t Definition = 0;
        std::uint8_t Style = 0, Owned = 0;
        std::uint16_t Flags = 0;
    };
    static_assert(sizeof(LoadoutEntry) == 16);
    template<class IsOurEmblem, class DestroyOwned>
    unsigned FilterLoadout(bool AssignedHero, LoadoutEntry* Entries, int& Count, IsOurEmblem IsOurs, DestroyOwned Destroy)
    {
        if (AssignedHero || !Entries || Count < 0 || Count > 256) return 0;
        unsigned Removed = 0;
        for (int I = 0; I < Count;)
        {
            if (!Entries[I].View || !IsOurs(Entries[I].View)) { ++I; continue; }
            if (Entries[I].Owned) Destroy(Entries[I].View);
            if (I + 1 < Count) std::memmove(Entries + I, Entries + I + 1, static_cast<std::size_t>(Count - I - 1) * sizeof(LoadoutEntry));
            --Count; ++Removed;
        }
        return Removed;
    }
    template<class Projectile>
    bool AddAttack(Projectile& P, int LocalHero, std::uint64_t Particle)
    {
        constexpr std::uint32_t Mask = 0x3fff;
        if (!Particle || LocalHero < 0 || LocalHero >= static_cast<int>(Mask) || !P.has_source() ||
            (P.source() & Mask) != static_cast<unsigned>(LocalHero) || !P.is_attack() || P.additional_particle_system_handle()) return false;
        P.set_additional_particle_system_handle(static_cast<std::int64_t>(Particle)); return true;
    }
}
