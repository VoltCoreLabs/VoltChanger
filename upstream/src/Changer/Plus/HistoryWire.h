#pragma once
#include "BattleData.h"
namespace HistoryWire
{
    // Does not touch output on malformed or unrelated responses.
    bool Decode(const std::string& Body, std::uint32_t Token, std::vector<BattleData::Match>& Matches);
}
