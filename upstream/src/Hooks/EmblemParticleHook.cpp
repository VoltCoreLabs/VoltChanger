#include "EmblemParticleHook.h"
#include "Changer/Match/EmblemEffect.h"
#include "Changer/Match/EmblemScope.h"
#include "SDK/Econ/C_EconItemView.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include "SDK/GameRules/C_DOTAGamerules.h"
#include "Misc/Patterns.h"
#include "SDK/Logger/Logger.h"
#include "SDK/Memory/Memory.h"
#include <MinHook.h>
#include <atomic>

namespace
{
    struct Loadout
    {
        int Count = 0, Pad = 0;
        EmblemScope::LoadoutEntry* Entries = nullptr;
        int Capacity = 0;
        unsigned Flags = 0;
    };
    static_assert(sizeof(Loadout) == 24);
    void (*Original)(C_DOTA_BaseNPC_Hero*, void*, void*, Loadout*) = nullptr;
    std::atomic<unsigned> Reports{0};
    void Detour(C_DOTA_BaseNPC_Hero* Hero, void* Second, void* Third, Loadout* Out)
    {
        Original(Hero, Second, Third, Out);
        if (!C_DOTAGamerules::Get() || !Hero || !Out || Out->Count < 0 || Out->Count > 256 || Out->Capacity < Out->Count) return;
        auto* Local = C_DOTAPlayerController::LocalHero();
        const auto Removed = EmblemScope::FilterLoadout(Hero == Local, Out->Entries, Out->Count,
            [](std::uintptr_t Pointer) { return EmblemEffect::IsLocalEmblem(reinterpret_cast<C_EconItemView*>(Pointer)); },
            [](std::uintptr_t Pointer) {
                // Exact native removal sequence in UpdateAssetModifiers:
                // entry+0xD owned -> View vfunc[2](View,1), then compact 16 bytes.
                // vfunc[0] is a copy operation, not the deleting destructor.
                auto* View = reinterpret_cast<C_EconItemView*>(Pointer);
                auto** Vtable = *reinterpret_cast<void***>(View);
                reinterpret_cast<void* (*)(C_EconItemView*, unsigned)>(Vtable[2])(View, 1);
            });
        if (Removed && Reports.fetch_add(1) < 40)
            Log("Emblem owner scope: hero=%d removed=%u local=%d", Hero->Index(), Removed, Local ? Local->Index() : -1);
    }
}
bool EmblemParticleHook::Install()
{
    const auto Target = Memory::FindPattern("client.dll", Patterns::HeroGatherLoadout);
    if (!Target) { LogE("Emblem owner-aware loadout gather pattern not found"); return false; }
    auto Status = MH_Initialize();
    if (Status != MH_OK && Status != MH_ERROR_ALREADY_INITIALIZED) return false;
    auto* Function = reinterpret_cast<void*>(Target);
    Status = MH_CreateHook(Function, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&Original));
    if (Status == MH_OK) Status = MH_EnableHook(Function);
    if (Status != MH_OK) { LogE("Emblem owner scope hook failed: %s", MH_StatusToString(Status)); return false; }
    Log("Emblem owner-aware loadout scope installed (local assigned hero only)"); return true;
}
