#include "Changer/Plus/PlayerSignals.h"
#include <iostream>
#include <stdexcept>

void Check(bool Value, const char* Message) { if (!Value) throw std::runtime_error(Message); }
BattleData::Match Match(unsigned Id, bool Won, unsigned Hero = 1, int Lobby = 7)
{
    BattleData::Match M; M.Id = Id; M.Started = Id * 100; M.Hero = Hero; M.Won = Won;
    M.Lobby = Lobby; M.Kills = 10; M.Assists = 10; M.Deaths = 2; return M;
}
int main()
{
    try
    {
        PlayerAnalysis::Snapshot P;
        PlayerSignals::Analyze(P);
        Check(!P.Patterns.CurrentStreak && !P.Patterns.HighWinRate && !P.Patterns.HighKda && P.Patterns.MedianKda < 0, "empty history must remain unknown");
        P.Matches = {Match(1, false, 2), Match(3, true), Match(4, true), Match(2, false, 2), Match(4, true), Match(0, true)};
        PlayerSignals::Analyze(P);
        Check(P.Matches.size() == 4 && P.Matches[0].Id == 4 && P.Wins == 2, "sort and deduplicate before computing sample");
        Check(P.Patterns.StreakWon && P.Patterns.CurrentStreak == 2 && P.Patterns.LongestWins == 2 && P.Patterns.LongestLosses == 2, "win/loss runs must follow chronological outcomes");
        P.Matches.push_back(Match(5, false)); PlayerSignals::Analyze(P);
        Check(!P.Patterns.StreakWon && P.Patterns.CurrentStreak == 1, "current losing streak must stop at first win");
        P.Matches.clear();
        for (unsigned I = 1; I <= 50; ++I) P.Matches.push_back(Match(I, true, I > 20 ? 2 : 1, I > 30 ? 0 : 7));
        PlayerSignals::Analyze(P);
        Check(P.Patterns.RankedSample == 30 && P.Patterns.KdaSample == 30 && P.Patterns.HighWinRate && P.Patterns.HighKda, "only ranked games inform performance flags");
        Check(P.RecentHeroes.size() == 1 && P.RecentHeroes[0].Hero == 2 && P.RecentHeroes[0].Games == 30, "frequent recent heroes must use only latest 30 available games");
        for (auto& M : P.Matches) M.Lobby = -1;
        PlayerSignals::Analyze(P);
        Check(!P.Patterns.RankedSample && !P.Patterns.HighWinRate && !P.Patterns.HighKda, "missing lobby must not count as ranked");
        P.Matches.clear(); for (unsigned I = 1; I <= 19; ++I) P.Matches.push_back(Match(I, true));
        PlayerSignals::Analyze(P);
        Check(!P.Patterns.HighWinRate && !P.Patterns.HighKda, "perfect tiny sample must not set performance flags");
        P.Matches.push_back(Match(20, true)); P.Matches.back().Kills = -1;
        PlayerSignals::Analyze(P);
        Check(P.Patterns.HighWinRate && !P.Patterns.HighKda && P.Patterns.KdaSample == 19, "missing KDA must not become zero or count toward threshold");
        Check(PlayerSignals::WilsonLower(0, 0) < 0 && PlayerSignals::WilsonLower(5, 4) < 0 && PlayerSignals::WilsonLower(20, 20) > .8, "Wilson interval must handle invalid and perfect samples");
        P.RecentHeroes = {{1, 20, 10}, {2, 1, 1}}; P.Frequent = {{3, 1000, 1000}};
        auto B = PlayerSignals::Bans({P});
        Check(B.size() == 2 && B[0].Hero == 1 && B[0].BanScore > B[1].BanScore, "frequent picks must outrank one win; prefer recent over lifetime");
        PlayerAnalysis::Snapshot Other; Other.Frequent = {{1, 10, 5}, {3, 10, 5}};
        auto Combined = PlayerSignals::Bans({P, Other});
        Check(Combined.size() == 3 && Combined[0].Hero == 1 && Combined[0].Games == 30 && Combined[0].Wins == 15, "merge opponents and use public aggregates when recent history unavailable");
        Check(PlayerSignals::Bans({{}}).empty(), "missing public data must produce no invented ban recommendations");
        std::cout << "Player history, performance indicators and ban scenarios passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; return 1; }
}
