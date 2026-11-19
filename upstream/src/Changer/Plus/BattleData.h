#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace BattleData
{
    struct Match
    {
        std::uint64_t Id = 0;
        std::uint32_t Started = 0, Hero = 0, Duration = 0;
        bool Won = false;
        int Kills = -1, Deaths = -1, Assists = -1, Gpm = -1, Xpm = -1;
        int Mode = -1, Lobby = -1, Lane = -1;
    };
    struct Snapshot
    {
        std::uint32_t Account = 0;
        std::uint64_t Revision = 0;
        bool Busy = false;
        std::string Status;
        std::vector<Match> Matches;
    };
    // Own public history only; at most 100 matches, with a one-minute retry limit.
    void Request(std::uint32_t Account);
    Snapshot Get();
}
