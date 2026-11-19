#pragma once
#include "BattleData.h"
#include <array>
namespace PlayerAnalysis
{
    struct HeroUsage
    {
        unsigned Hero = 0, Games = 0, Wins = 0;
        std::uint32_t LastPlayed = 0;
        double BanScore = 0;
    };
    struct Signals
    {
        unsigned CurrentStreak = 0, LongestWins = 0, LongestLosses = 0;
        bool StreakWon = false;
        unsigned RankedSample = 0, RankedWins = 0, KdaSample = 0;
        double MedianKda = -1, WinLowerBound = -1;
        bool HighWinRate = false, HighKda = false;
        std::string Assessment;
    };
    struct Snapshot
    {
        unsigned Account = 0;
        std::uint64_t Revision = 0;
        bool Busy = false;
        std::string Status, Role;
        std::vector<BattleData::Match> Matches;
        std::array<unsigned, 5> Lanes{};
        unsigned Wins = 0;
        std::vector<HeroUsage> Frequent, RecentHeroes;
        Signals Patterns;
        std::string HeroSource;
        bool HistoricalOnly = false;
        bool Cached = false;
        std::uint64_t FetchedAt = 0;
    };
    void Request(unsigned Account, bool Select = true);
    Snapshot Get();
    Snapshot Get(unsigned Account);
}
