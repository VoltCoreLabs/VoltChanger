#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace MetaData
{
    struct Counts { double Picks = 0, Wins = 0; double WinRate() const { return Picks > 0 ? Wins * 100 / Picks : -1; } };
    struct Hero
    {
        unsigned Id = 0;
        std::string Name, Internal;
        std::array<Counts, 9> Ranks{}; // 0 public total, 1..8 OpenDota brackets.
        std::array<Counts, 5> Lanes{}; // same 900-second scenario slice, no duplicate time buckets.
    };
    struct Snapshot
    {
        std::uint64_t Revision = 0, Updated = 0;
        bool Busy = false;
        std::string Status, LaneStatus;
        std::vector<Hero> Heroes;
    };
    struct Facet { unsigned Variant = 0; Counts Stats; };
    struct Counter { unsigned Hero = 0; Counts Stats; };
    struct Detail
    {
        unsigned HeroId = 0;
        std::uint64_t Revision = 0;
        bool Busy = false;
        std::string Status;
        std::vector<Facet> Facets;
        std::vector<Counter> Matchups;
        unsigned ParsedMatches = 0;
    };
    void Request();
    Snapshot Get();
    void RequestDetail(unsigned Hero);
    Detail GetDetail();
    // Pick rate is the estimated share of games: each ordinary match has 10 picks.
    double PickRate(const std::vector<Hero>& Heroes, const Hero& H, unsigned Rank);
    std::vector<Hero> Sorted(const Snapshot& S, unsigned Rank, unsigned Lane, unsigned Sort, bool Ascending);
}
