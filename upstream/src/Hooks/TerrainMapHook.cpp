#include "TerrainMapHook.h"
#include "Changer/Match/LoadoutOverride.h"
#include "Changer/Match/TerrainScope.h"
#include "SDK/Econ/C_EconItemView.h"
#include "SDK/Memory/Memory.h"
#include "SDK/Logger/Logger.h"
#include <MinHook.h>
#include <atomic>

namespace
{
    bool (*OriginalCallback)(void*, const char*, void*, C_EconItemView**) = nullptr;
    bool (*OriginalResolver)(void*, C_EconItemView*, const char*, void*) = nullptr;
    struct Invocation
    {
        unsigned Calls = 0;
        C_EconItemView* Substituted = nullptr;
    };
    thread_local Invocation* Active = nullptr;
    std::atomic<unsigned> CallbackLogs{0}, ResolverLogs{0};
    bool Resolver(void* System, C_EconItemView* Item, const char* Input, void* Output)
    {
        if (Active) ++Active->Calls;
        const bool NativeResult = OriginalResolver(System, Item, Input, Output);
        if (NativeResult || !Active || !Output || !TerrainScope::IsBaseMap(Input)) return NativeResult;
        // World callbacks reach here only after native addon/engine gates.
        // Other resolver callers (e.g. overview) never enter this fallback.
        C_EconItemView* Selected = nullptr;
        const auto Result = TerrainScope::TryCurrentSelection(NativeResult, true, true, Input, Item, Active->Substituted,
            [&] { Selected = LoadoutOverride::SelectedTerrainView(); return Selected; },
            [&](auto* View) { return OriginalResolver(System, View, Input, Output); });
        if (ResolverLogs.fetch_add(1) < 80)
            Log("Terrain resolver: input=%s native=0 item=%u selected=%u fallback=%d", Input, Item ? Item->ItemDefinitionIndex() : 0, Selected ? Selected->ItemDefinitionIndex() : 0, Result);
        return Result;
    }
    bool Callback(void* Secondary, const char* Input, void* Output, C_EconItemView** Used)
    {
        Invocation Frame; auto* Previous = Active; Active = &Frame;
        const bool Result = OriginalCallback(Secondary, Input, Output, Used);
        Active = Previous;
        if (Result && Used && Frame.Substituted) *Used = Frame.Substituted;
        if (CallbackLogs.fetch_add(1) < 80)
            Log("Terrain asset callback: input=%s result=%d resolver_calls=%u used=%u", Input ? Input : "?", Result, Frame.Calls, Result && Used && *Used ? (*Used)->ItemDefinitionIndex() : 0);
        return Result;
    }
}
bool TerrainMapHook::Install()
{
    constexpr const char* CallbackPattern = "40 55 56 41 54 41 56 41 57 48 83 EC 30 45 33 E4 49 8B F1 4D 89 21 49 8B E8 4C 8B F2 4C 8B F9";
    constexpr const char* ResolverPattern = "40 53 55 48 83 EC 58 49 8B E9 48 8B DA 48 85 D2 0F 84 ? ? ? ? 80 7A 30 00 0F 84 ? ? ? ?";
    const auto ResolveTarget = Memory::FindPattern("client.dll", ResolverPattern);
    const auto CallbackTarget = Memory::FindPattern("client.dll", CallbackPattern);
    if (!ResolveTarget || !CallbackTarget) { LogW("Terrain native resolver patterns unavailable; retaining ordinary terrain handling"); return true; }
    auto Status = MH_Initialize(); if (Status != MH_OK && Status != MH_ERROR_ALREADY_INITIALIZED) return false;
    auto* Resolve = reinterpret_cast<void*>(ResolveTarget); auto* Asset = reinterpret_cast<void*>(CallbackTarget);
    Status = MH_CreateHook(Resolve, reinterpret_cast<void*>(Resolver), reinterpret_cast<void**>(&OriginalResolver));
    if (Status == MH_OK) Status = MH_CreateHook(Asset, reinterpret_cast<void*>(Callback), reinterpret_cast<void**>(&OriginalCallback));
    if (Status == MH_OK) Status = MH_EnableHook(Resolve);
    if (Status == MH_OK) Status = MH_EnableHook(Asset);
    if (Status != MH_OK)
    {
        MH_DisableHook(Resolve); MH_DisableHook(Asset);
        LogE("Terrain native resolver hook failed: %s", MH_StatusToString(Status)); return false;
    }
    Log("Terrain native resolver and asset callback installed"); return true;
}
