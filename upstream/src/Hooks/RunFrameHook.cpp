#include "RunFrameHook.h"

#include "Changer/Match/EmblemEffect.h"
#include "Changer/Match/ImpactEffects.h"
#include "Changer/Inventory/InventorySync.h"
#include "Changer/Match/NetSkins.h"
#include "Changer/Plus/LocalPlus.h"
#include "Changer/Plus/AssistantHud.h"
#include "Changer/Plus/BattleStats.h"
#include "Changer/Plus/MetaPanel.h"
#include "Changer/Plus/MatchPlayersPanel.h"
#include "Changer/Plus/VisionTracker.h"
#include "Changer/Plus/NativeIntegration.h"
#include "Changer/Plus/HeroPageTier.h"
#include "Changer/Plus/HeroBadgeSync.h"
#include "Changer/Plus/MinimapOverlay.h"
#include "Changer/Ui/TreasureAddButton.h"
#include "Changer/Ui/UnusualSettings.h"
#include "Hooks/GcRecvHook.h"
#include "Misc/Patterns.h"
#include "SDK/Logger/Logger.h"
#include "SDK/Memory/Memory.h"
#include "SDK/V8/V8Dumper.h"

#include <MinHook.h>

#include <cstdint>

namespace
{
    void (*Original)(void*) = nullptr;
    bool Dumped = false;
    bool FirstFrame = true;

    void Detour(void* Engine)
    {
        const bool Trace = FirstFrame; FirstFrame = false;
        if (Trace) Log("Startup frame: begin inventory preparation");
        InventorySync::Update();
        LocalPlus::Update();
        if (Trace) Log("Startup frame: entering native Panorama frame");
        Original(Engine);
        if (Trace) Log("Startup frame: native Panorama frame completed");
        GcRecvHook::Update();
        InventorySync::PublishCatalog();

        if (!Dumped)
        {
            Dumped = true;
            V8Dumper::Run();
        }

        NetSkins::Update();
        AssistantHud::Update();
        BattleStats::Update();
        VisionTracker::Update();
        MinimapOverlay::Update();
        MetaPanel::Update();
        MatchPlayersPanel::Update();
        NativeIntegration::Update();
        HeroPageTier::Update();
        HeroBadgeSync::Update();
        if (Trace) Log("Startup frame: assistant and hero page updates completed");
        EmblemEffect::Update();
        ImpactEffects::Update();
        UnusualSettings::Update();
        TreasureAddButton::Update();
    }
}

bool RunFrameHook::Install()
{
    std::uintptr_t Target = Memory::FindPattern("panorama.dll", Patterns::RunFrame);
    if (!Target)
    {
        LogE("RunFrame pattern not found");
        return false;
    }

    MH_STATUS Status = MH_Initialize();
    if (Status != MH_OK && Status != MH_ERROR_ALREADY_INITIALIZED)
    {
        LogE("MinHook init failed: %s", MH_StatusToString(Status));
        return false;
    }

    auto* TargetFn = reinterpret_cast<void*>(Target);
    Status = MH_CreateHook(TargetFn, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&Original));
    if (Status == MH_OK)
        Status = MH_EnableHook(TargetFn);

    if (Status != MH_OK)
    {
        LogE("RunFrame hook failed: %s", MH_StatusToString(Status));
        return false;
    }

    Log("RunFrame %#llx", static_cast<unsigned long long>(Target));
    return true;
}
