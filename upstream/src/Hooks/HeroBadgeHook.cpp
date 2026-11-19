#include "HeroBadgeHook.h"
#include "Changer/Plus/HeroProgress.h"
#include "Changer/Plus/HeroProgressPlan.h"
#include "Changer/Plus/LocalPlus.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include "SDK/Memory/Memory.h"
#include "SDK/Logger/Logger.h"
#include <MinHook.h>
#include <atomic>

namespace
{
    using Getter = unsigned (*)(void*, int);
    Getter OriginalTier = nullptr, OriginalLevel = nullptr, OriginalXP = nullptr;
    const unsigned* (*TierDescriptor)(unsigned) = nullptr;
    unsigned (*TotalXP)(unsigned) = nullptr;
    std::atomic<bool> Installed{false};
    std::atomic<std::uint64_t> LastReported{0};
    std::atomic<std::uint64_t> RealTier{0};
    unsigned Desired(void* Resource, int Player)
    {
        if (!Resource || !Installed || !LocalPlus::Enabled()) return 0;
        auto* Hero = C_DOTAPlayerController::LocalHero(); if (!Hero) return 0;
        const auto Level = HeroProgress::Level(Hero->HeroId());
        if (!HeroProgressPlan::OwnPlayer(Player, Hero->PlayerID(), Hero->HeroId(), Level)) return 0;
        const auto Stamp = (static_cast<std::uint64_t>(Hero->HeroId()) << 32) | (static_cast<unsigned>(Player) << 8) | Level;
        if (LastReported.exchange(Stamp) != Stamp) Log("Native local hero badge override: hero=%u player=%d level=%u", Hero->HeroId(), Player, Level);
        return Level;
    }
    unsigned Tier(void* Resource, int Player)
    {
        const auto Native = OriginalTier(Resource, Player);
        if (const auto Level = Desired(Resource, Player))
        {
            auto* Hero = C_DOTAPlayerController::LocalHero();
            if (Hero && Native <= 6) RealTier = (static_cast<std::uint64_t>(Hero->Handle().Value) << 32) | (static_cast<unsigned>(Player + 1) << 8) | Native;
            const auto* Record = TierDescriptor(Level);
            if (Record && *Record >= 1 && *Record <= 6) return *Record;
        }
        return Native;
    }
    unsigned Level(void* Resource, int Player)
    {
        const auto Selected = Desired(Resource, Player); return Selected ? Selected : OriginalLevel(Resource, Player);
    }
    unsigned XP(void* Resource, int Player)
    {
        const auto Selected = Desired(Resource, Player); return Selected ? TotalXP(Selected) : OriginalXP(Resource, Player);
    }
}
bool HeroBadgeHook::Ready() { return Installed.load(); }
bool HeroBadgeHook::AllowsLocalVoice(unsigned Hero, unsigned RequiredTier)
{
    if (!Ready() || !LocalPlus::Enabled() || !Hero || !RequiredTier || RequiredTier > 6) return false;
    const auto Wanted = HeroProgress::Level(Hero); if (!Wanted) return false;
    const auto* Record = TierDescriptor(Wanted);
    return Record && *Record >= RequiredTier;
}
bool HeroBadgeHook::NeedsLocalVoice(unsigned RequiredHero, unsigned RequiredTier)
{
    if (!Ready() || !LocalPlus::Enabled() || !RequiredTier || RequiredTier > 6) return false;
    auto* Hero = C_DOTAPlayerController::LocalHero();
    if (!Hero || Hero->HeroId() != RequiredHero || Hero->PlayerID() < 0 || Hero->PlayerID() >= 24) return false;
    const auto Captured = RealTier.load();
    if (static_cast<std::uint32_t>(Captured >> 32) != Hero->Handle().Value || ((Captured >> 8) & 255) != static_cast<unsigned>(Hero->PlayerID() + 1))
    {
        static std::atomic<unsigned> Missing{0};
        if (Missing.fetch_add(1) < 12) LogW("Hero voice gate: no matching native-tier capture hero=%u player=%d", RequiredHero, Hero->PlayerID());
        return false;
    }
    const auto Wanted = HeroProgress::Level(Hero->HeroId()); if (!Wanted) return false;
    const auto* Record = TierDescriptor(Wanted);
    const bool Allowed = Record && *Record >= RequiredTier && (Captured & 255) < RequiredTier;
    static std::atomic<unsigned> Reports{0};
    if (Reports.fetch_add(1) < 64) Log("Hero voice gate: hero=%u required=%u local=%u native=%u preview=%d", RequiredHero, RequiredTier, Record ? *Record : 0, static_cast<unsigned>(Captured & 255), Allowed);
    return Allowed;
}
bool HeroBadgeHook::Install()
{
    constexpr const char* TierPattern = "48 83 EC 28 4C 8B C1 85 D2 74 02 78 5A 3B 91 70 06 00 00 7D 52";
    constexpr const char* LevelPattern = "4C 8B C1 85 D2 74 02 78 4C 3B 91 70 06 00 00 7D 44 49 8B 80 78 06 00 00 4C 63 CA 49 69 C9 F0 00 00 00 8B 54 01 40 8D 42 FE 83 F8 01 76 0D 8D 42 FA 83 F8 07 76 05 83 FA 0E 75 1A 49 8B 80 10 06 00 00 49 69 C9 38 02 00 00 8B 8C 01 A8 01 00 00 E9 ? ? ? ?";
    constexpr const char* XPPattern = "4C 8B C1 85 D2 74 02 78 48 3B 91 70 06 00 00 7D 40 49 8B 80 78 06 00 00 4C 63 CA 49 69 C9 F0 00 00 00 8B 54 01 40 8D 42 FE 83 F8 01 76 0D 8D 42 FA 83 F8 07 76 05 83 FA 0E 75 16 49 8B 80 10 06 00 00 49 69 C9 38 02 00 00 8B 84 01 A8 01 00 00 C3";
    constexpr const char* DescriptorPattern = "BA 06 00 00 00 48 8D 05 ? ? ? ? 4C 8D 05 ? ? ? ? 3B 08 73 13 FF CA 48 83 E8 18 49 3B C0";
    constexpr const char* ThresholdPattern = "40 53 48 83 EC 20 8B D9 83 F9 1E 76 08 33 C0 48 83 C4 20 5B C3 E8 ? ? ? ? 8B 04 98";
    auto* TierTarget = reinterpret_cast<void*>(Memory::FindPattern("client.dll", TierPattern));
    auto* LevelTarget = reinterpret_cast<void*>(Memory::FindPattern("client.dll", LevelPattern));
    auto* XPTarget = reinterpret_cast<void*>(Memory::FindPattern("client.dll", XPPattern));
    TierDescriptor = reinterpret_cast<decltype(TierDescriptor)>(Memory::FindPattern("client.dll", DescriptorPattern));
    TotalXP = reinterpret_cast<decltype(TotalXP)>(Memory::FindPattern("client.dll", ThresholdPattern));
    if (!TierTarget || !LevelTarget || !XPTarget || !TierDescriptor || !TotalXP)
    {
        LogW("Native local hero badge patterns unavailable; preserving native progress"); return true;
    }
    auto Status = MH_Initialize(); if (Status != MH_OK && Status != MH_ERROR_ALREADY_INITIALIZED) return false;
    Status = MH_CreateHook(TierTarget, reinterpret_cast<void*>(Tier), reinterpret_cast<void**>(&OriginalTier));
    if (Status == MH_OK) Status = MH_CreateHook(LevelTarget, reinterpret_cast<void*>(Level), reinterpret_cast<void**>(&OriginalLevel));
    if (Status == MH_OK) Status = MH_CreateHook(XPTarget, reinterpret_cast<void*>(XP), reinterpret_cast<void**>(&OriginalXP));
    if (Status == MH_OK) Status = MH_EnableHook(TierTarget);
    if (Status == MH_OK) Status = MH_EnableHook(LevelTarget);
    if (Status == MH_OK) Status = MH_EnableHook(XPTarget);
    if (Status != MH_OK)
    {
        MH_DisableHook(TierTarget); MH_DisableHook(LevelTarget); MH_DisableHook(XPTarget);
        LogE("Native hero badge hook failed: %s", MH_StatusToString(Status)); return false;
    }
    Installed = true;
    Log("Native local hero badge tier/level/XP hooks installed"); return true;
}
