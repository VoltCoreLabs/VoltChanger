#pragma once
#include "BattleData.h"
namespace NativeHistory
{
    void Request();
    BattleData::Snapshot Get();
    // Observes real incoming messages without changing or suppressing them.
    void Receive(std::uint32_t EMsg, const std::string& Body);
}
