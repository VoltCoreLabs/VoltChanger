#include "Changer/Plus/MatchPlayerSummary.h"
#include "Changer/Plus/MatchPanelPlan.h"
#include <cstdlib>
#include <iostream>

namespace
{
    void Check(bool Value, const char* Message)
    {
        if (!Value) { std::cerr << Message << '\n'; std::exit(1); }
    }
}
int main()
{
    using namespace MatchPlayerSummary;
    Check(MatchPanelPlan::Picking(EGameState::HeroSelection) && MatchPanelPlan::Picking(EGameState::PlayerDraft)
          && MatchPanelPlan::Picking(EGameState::StrategyTime), "player history panel must be available before the match HUD scoreboard");
    Check(!MatchPanelPlan::Picking(EGameState::GameInProgress) && !MatchPanelPlan::Picking(EGameState::PostGame)
          && !MatchPanelPlan::Picking(EGameState::Unknown), "draft overlay must not auto-open in gameplay, postgame or main menu");
    Check(PublicAccount("76561197960265729") == 1, "SteamID64 must retain integer precision");
    Check(PublicAccount("76561202255233023") == UINT32_MAX, "largest individual account");
    for (const char* Invalid : {"", "0", "12345", "76561197960265728", "76561202255233024", "76561197960265729x", " 76561197960265729", "-1"})
        Check(PublicAccount(Invalid) == 0, "masked, malformed or out-of-range IDs must not be inferred");
    PlayerAnalysis::Snapshot Data;
    // More than 15 matches, deliberately oldest first. Old games must not affect
    // the displayed current sample or its lane/hero frequencies.
    for (unsigned I = 1; I <= 18; ++I)
    {
        BattleData::Match M; M.Id = I; M.Started = 100 + I;
        M.Hero = I >= 10 ? 2 : 1; M.Won = I % 2 == 0;
        M.Lane = I >= 10 ? 2 : -1; Data.Matches.push_back(M);
    }
    Data.Matches.push_back(Data.Matches.back());
    Data.Matches.push_back({});
    Data.Frequent.push_back({99, 999, 999});
    const auto S = Build(Data, 1);
    Check(S.Matches.size() == 15 && S.Matches.front().Id == 18 && S.Matches.back().Id == 4, "newest 15 distinct nonzero match IDs");
    Check(S.Wins == 8, "sample W/L");
    Check(S.Favorite.Id == 2 && S.Favorite.Games == 9 && S.Favorite.Wins == 5, "favorite based on the displayed sample");
    Check(S.Popular.size() == 2 && S.Popular[0].Id == 2 && S.Popular[1].Id == 1,
          "frequent hero list must use the same last-15 sample in frequency order");
    Check(S.Active.Id == 1 && S.Active.Games == 6 && S.Active.Wins == 3, "current hero sample");
    Check(S.PickRate(S.Favorite) == 60 && S.PickRate(S.Active) == 40 && S.Active.WinRate() == 50, "sample PR/WR denominator");
    Check(S.BestLane == 2 && S.LaneSample == 9 && S.Lanes[2] == 9, "unknown lanes excluded from known lane denominator");
    const auto Absent = Build(Data, 999);
    Check(Absent.Active.Games == 0 && Absent.Active.WinRate() == -1 && Absent.PickRate(Absent.Active) == 0, "unseen current hero has no invented WR");
    const auto Empty = Build({}, 1);
    Check(Empty.WinRate() == -1 && Empty.BestLane == 0 && Empty.LaneSample == 0 && Empty.PickRate(Empty.Active) == -1, "missing public history remains unavailable");
    PlayerAnalysis::Snapshot Tie;
    BattleData::Match A; A.Id = 1; A.Started = 1; A.Hero = 1;
    BattleData::Match B = A; B.Id = 2; B.Started = 2; B.Hero = 2;
    Tie.Matches = {A, B};
    Check(Build(Tie, 0).Favorite.Id == 2, "favorite tie uses the most recently played hero");
    std::cout << "match_player_summary: passed\n";
}
