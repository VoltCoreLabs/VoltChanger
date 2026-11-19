#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace ProTrackerData
{
    using Names = std::map<unsigned, std::string>;
    struct Item { unsigned Id = 0; std::string Name; double Pick = -1, Minute = -1; };
    struct Skill { unsigned Level = 0; std::string Name; };
    struct Talent { unsigned Level = 0; std::string Left, Right; double LeftPick = -1, RightPick = -1; };
    struct Snapshot
    {
        unsigned Hero = 0, Role = 0, Matches = 0;
        std::uint64_t Revision = 0;
        bool Busy = false, Cached = false, HasBuild = false, Statistical = false;
        double WinRate = -1;
        std::string Status, Updated, Source, SourceUrl;
        std::array<std::vector<Item>, 4> Stages;
        std::vector<Skill> Skills;
        std::vector<Talent> Talents;
    };
    Snapshot Parse(const std::string& Json, unsigned Hero, unsigned Role, const Names& Items, const Names& Abilities);
    void Request(unsigned Hero, unsigned Role);
    Snapshot Get(unsigned Hero, unsigned Role);
    bool Enabled();
    void Toggle();
    unsigned Role(unsigned Hero);
    void SetRole(unsigned Hero, unsigned Role);
}
