#include "Hooks.h"

#include "EconWearableSpawnHook.h"
#include "EmblemParticleHook.h"
#include "GcRecvHook.h"
#include "GcSendHook.h"
#include "GemRulesHook.h"
#include "ItemInLoadoutHook.h"
#include "NetFilterHook.h"
#include "SequenceDataChangedHook.h"
#include "RunFrameHook.h"
#include "TerrainMapHook.h"
#include "HeroBadgeHook.h"
#include "HeroVoiceWheelHook.h"

bool Hooks::Install()
{
    bool Ok = true;
    Ok &= RunFrameHook::Install();
    Ok &= GcSendHook::Install();
    Ok &= GcRecvHook::Install();
    Ok &= EconWearableSpawnHook::Install();
    Ok &= EmblemParticleHook::Install();
    Ok &= GemRulesHook::Install();
    Ok &= ItemInLoadoutHook::Install();
    Ok &= TerrainMapHook::Install();
    Ok &= HeroBadgeHook::Install();
    Ok &= HeroVoiceWheelHook::Install();
    Ok &= NetFilterHook::Install();
    Ok &= SequenceDataChangedHook::Install();
    return Ok;
}
