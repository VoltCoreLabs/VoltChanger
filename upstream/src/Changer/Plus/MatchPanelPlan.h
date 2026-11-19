#pragma once
#include "SDK/GameRules/C_DOTAGamerules.h"
namespace MatchPanelPlan
{
    constexpr bool Picking(EGameState State)
    { return State == EGameState::HeroSelection || State == EGameState::StrategyTime || State == EGameState::PlayerDraft; }
}
