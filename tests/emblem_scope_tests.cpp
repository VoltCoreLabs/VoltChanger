#include "Changer/Match/EmblemScope.h"
#include "dota_usermessages.pb.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>

void Check(bool Value, const char* Message) { if (!Value) throw std::runtime_error(Message); }
template<class Projectile> void Projectiles()
{
    Projectile Own; Own.set_source((9u << 14) | 720); Own.set_is_attack(true); Own.set_particle_system_handle(123);
    Check(EmblemScope::AddAttack(Own, 720, 456) && Own.additional_particle_system_handle() == 456 && Own.particle_system_handle() == 123, "assigned hero attack receives only additional emblem particle");
    Projectile Other; Other.set_source(721); Other.set_is_attack(true); Other.set_particle_system_handle(123);
    const auto Bytes = Other.SerializeAsString();
    Check(!EmblemScope::AddAttack(Other, 720, 456) && Other.SerializeAsString() == Bytes, "another hero/illusion must remain unchanged regardless of swapped model");
    Check(!EmblemScope::AddAttack(Other, -1, 456) && Other.SerializeAsString() == Bytes, "missing assigned hero must not authorize a different source");
    Own.set_additional_particle_system_handle(999); const auto Existing = Own.SerializeAsString();
    Check(!EmblemScope::AddAttack(Own, 720, 456) && Own.SerializeAsString() == Existing, "server-provided additional effect must not be replaced");
    Own.clear_additional_particle_system_handle(); Own.set_is_attack(false); const auto Spell = Own.SerializeAsString();
    Check(!EmblemScope::AddAttack(Own, 720, 456) && Own.SerializeAsString() == Spell, "non-attack projectile must remain unchanged");
    Own.clear_source(); Own.set_is_attack(true);
    Check(!EmblemScope::AddAttack(Own, 0, 456), "missing source must not turn into entity zero");
}
int main()
{
    try
    {
        using Entry = EmblemScope::LoadoutEntry;
        // Pointer stand-ins: 101/102/103 are copies of our fake emblem; 201 is a
        // real player's emblem with the SAME definition, 301 another item.
        std::array<Entry, 6> Entries{{{101, 18379, 0, 1}, {201, 18379, 0, 1}, {102, 18379, 0, 0}, {0, 123, 2, 0}, {301, 12, 0, 1}, {103, 18379, 0, 1}}};
        const auto Original = Entries; int Count = 6; std::vector<std::uintptr_t> Destroyed;
        auto Ours = [](std::uintptr_t P) { return P == 101 || P == 102 || P == 103; };
        auto Destroy = [&](std::uintptr_t P) { Destroyed.push_back(P); };
        Check(!EmblemScope::FilterLoadout(true, Entries.data(), Count, Ours, Destroy) && Count == 6 && Destroyed.empty(), "assigned hero keeps full native loadout without deleting owned entries");
        Check(EmblemScope::FilterLoadout(false, Entries.data(), Count, Ours, Destroy) == 3 && Count == 3, "other heroes must lose only our fake emblem entries");
        Check(Entries[0].View == 201 && Entries[1].View == 0 && Entries[1].Definition == 123 && Entries[1].Style == 2 && Entries[2].View == 301, "real same-definition emblems, static records and other cosmetics must retain order and metadata");
        Check(Destroyed == std::vector<std::uintptr_t>{101, 103}, "only removed owned copies invoke native deletion; borrowed entries must not be freed");
        Check(!EmblemScope::FilterLoadout(false, Entries.data(), Count, Ours, Destroy) && Count == 3, "repeated filtering must not double-delete records");
        Entries = Original; Count = 6; Destroyed.clear();
        Check(!EmblemScope::FilterLoadout(false, Entries.data(), Count, [](auto) { return false; }, Destroy) && Count == 6 && Destroyed.empty(), "unequipped/unrecognized item IDs must remain unchanged");
        Count = 257;
        Check(!EmblemScope::FilterLoadout(false, Entries.data(), Count, Ours, Destroy), "unknown native vector layout must be rejected before access");
        Projectiles<CDOTAUserMsg_TE_Projectile>(); Projectiles<CDOTAUserMsg_TE_ProjectileLoc>();
        std::cout << "Emblem owner, aura and both projectile paths passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; return 1; }
}
