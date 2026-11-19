#pragma once
#include "PlayerAnalysis.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <set>
#include <string_view>

namespace MatchPlayerSummary
{
    inline unsigned PublicAccount(std::string_view Steam)
    {
        constexpr std::uint64_t Base = 76561197960265728ull;
        std::uint64_t Id = 0; const auto Parsed = std::from_chars(Steam.data(), Steam.data() + Steam.size(), Id);
        return Parsed.ec == std::errc() && Parsed.ptr == Steam.data() + Steam.size() && Id > Base && Id <= Base + UINT32_MAX ? static_cast<unsigned>(Id - Base) : 0;
    }
    struct Hero
    {
        unsigned Id = 0, Games = 0, Wins = 0;
        std::uint32_t Latest = 0;
        double WinRate() const { return Games ? Wins * 100. / Games : -1; }
    };
    struct Summary
    {
        std::vector<BattleData::Match> Matches;
        unsigned Wins = 0;
        std::array<unsigned, 5> Lanes{};
        Hero Favorite, Active;
        std::vector<Hero> Popular;
        unsigned BestLane = 0, LaneSample = 0;
        double WinRate() const { return Matches.empty() ? -1 : Wins * 100. / Matches.size(); }
        double PickRate(const Hero& H) const { return Matches.empty() ? -1 : H.Games * 100. / Matches.size(); }
    };
    inline Summary Build(const PlayerAnalysis::Snapshot& Data, unsigned ActiveHero)
    {
        Summary Out; Out.Active.Id = ActiveHero;
        auto Matches = Data.Matches;
        std::sort(Matches.begin(), Matches.end(), [](const auto& A, const auto& B) { return A.Started == B.Started ? A.Id > B.Id : A.Started > B.Started; });
        std::set<std::uint64_t> Seen;
        std::map<unsigned, Hero> Usage;
        for (const auto& M : Matches)
        {
            if (!M.Id || !Seen.insert(M.Id).second) continue;
            Out.Matches.push_back(M); if (M.Won) ++Out.Wins;
            auto& H = Usage[M.Hero]; H.Id = M.Hero; ++H.Games; if (M.Won) ++H.Wins; H.Latest = std::max(H.Latest, M.Started);
            if (M.Lane >= 1 && M.Lane <= 4) { ++Out.Lanes[M.Lane]; ++Out.LaneSample; }
            if (Out.Matches.size() == 15) break;
        }
        for (const auto& [Id, H] : Usage)
        {
            if (Id) Out.Popular.push_back(H);
            if (H.Games > Out.Favorite.Games || (H.Games == Out.Favorite.Games && (H.Latest > Out.Favorite.Latest || (H.Latest == Out.Favorite.Latest && H.Id < Out.Favorite.Id)))) Out.Favorite = H;
            if (Id == ActiveHero) Out.Active = H;
        }
        std::sort(Out.Popular.begin(), Out.Popular.end(), [](const auto& A, const auto& B) {
            if (A.Games != B.Games) return A.Games > B.Games;
            return A.Latest == B.Latest ? A.Id < B.Id : A.Latest > B.Latest;
        });
        for (unsigned I = 1; I <= 4; ++I) if (Out.Lanes[I] > Out.Lanes[Out.BestLane]) Out.BestLane = I;
        return Out;
    }
}
