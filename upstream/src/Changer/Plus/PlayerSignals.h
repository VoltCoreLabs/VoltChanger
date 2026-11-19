#pragma once
#include "PlayerAnalysis.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace PlayerSignals
{
    inline double WilsonLower(unsigned Wins, unsigned Games)
    {
        if (!Games || Wins > Games) return -1;
        constexpr double Z = 1.96;
        const double N = Games, P = Wins / N, D = 1 + Z * Z / N;
        return std::max(0., (P + Z * Z / (2 * N) - Z * std::sqrt(P * (1 - P) / N + Z * Z / (4 * N * N))) / D);
    }
    inline void Analyze(PlayerAnalysis::Snapshot& Out)
    {
        std::sort(Out.Matches.begin(), Out.Matches.end(), [](const auto& A, const auto& B) { return A.Started == B.Started ? A.Id > B.Id : A.Started > B.Started; });
        std::set<std::uint64_t> Seen;
        std::erase_if(Out.Matches, [&](const auto& M) { return !M.Id || !Seen.insert(M.Id).second; });
        Out.Wins = 0; Out.Patterns = {}; Out.RecentHeroes.clear();
        std::map<unsigned, PlayerAnalysis::HeroUsage> Heroes;
        std::vector<double> Kdas;
        unsigned WinRun = 0, LossRun = 0;
        for (unsigned I = 0; I < Out.Matches.size(); ++I)
        {
            const auto& M = Out.Matches[I]; auto& P = Out.Patterns;
            if (M.Won) { ++Out.Wins; ++WinRun; LossRun = 0; } else { ++LossRun; WinRun = 0; }
            P.LongestWins = std::max(P.LongestWins, WinRun); P.LongestLosses = std::max(P.LongestLosses, LossRun);
            if (!I) P.StreakWon = M.Won;
            if (I == P.CurrentStreak && M.Won == P.StreakWon) ++P.CurrentStreak;
            if (I < 30)
            {
                auto& H = Heroes[M.Hero]; H.Hero = M.Hero; ++H.Games; if (M.Won) ++H.Wins; H.LastPlayed = std::max(H.LastPlayed, M.Started);
            }
            // Only ranked matches inform performance flags. Missing lobby data
            // or non-ranked modes cannot silently become ranked evidence.
            if (M.Lobby == 7 && P.RankedSample < 40)
            {
                ++P.RankedSample; if (M.Won) ++P.RankedWins;
                if (M.Kills >= 0 && M.Deaths >= 0 && M.Assists >= 0) Kdas.push_back((M.Kills + M.Assists) / static_cast<double>(std::max(1, M.Deaths)));
            }
        }
        for (auto& [Id, H] : Heroes)
        {
            H.BanScore = H.Games * ((H.Wins + 3.) / (H.Games + 6.)); Out.RecentHeroes.push_back(H);
        }
        std::sort(Out.RecentHeroes.begin(), Out.RecentHeroes.end(), [](const auto& A, const auto& B) { return A.Games == B.Games ? A.Hero < B.Hero : A.Games > B.Games; });
        auto& P = Out.Patterns; P.KdaSample = static_cast<unsigned>(Kdas.size());
        if (!Kdas.empty()) { std::sort(Kdas.begin(), Kdas.end()); P.MedianKda = Kdas.size() % 2 ? Kdas[Kdas.size() / 2] : (Kdas[Kdas.size() / 2 - 1] + Kdas[Kdas.size() / 2]) / 2; }
        P.WinLowerBound = WilsonLower(P.RankedWins, P.RankedSample);
        P.HighWinRate = P.RankedSample >= 20 && P.RankedWins * 100 >= P.RankedSample * 75 && P.WinLowerBound >= .60;
        P.HighKda = P.KdaSample >= 20 && P.MedianKda >= 5;
        P.Assessment = P.RankedSample < 20 ? "Недостаточно данных для оценки: нужно 20 доступных рейтинговых матчей."
            : P.HighWinRate && P.HighKda ? "Выраженные показатели в выборке. Возможный признак смурфа, не подтверждение."
            : P.HighWinRate || P.HighKda ? "Один выраженный показатель. Этого недостаточно для вывода о смурфе."
            : "По выбранным показателям выраженных признаков нет. Это не исключает смурф-аккаунт.";
    }
    inline std::vector<PlayerAnalysis::HeroUsage> Bans(const std::vector<PlayerAnalysis::Snapshot>& Players)
    {
        std::map<unsigned, PlayerAnalysis::HeroUsage> Scores;
        for (const auto& P : Players)
        {
            const auto& Source = P.RecentHeroes.empty() ? P.Frequent : P.RecentHeroes;
            double Total = 0; for (const auto& H : Source) Total += H.Games;
            if (Total <= 0) continue;
            for (const auto& H : Source)
            {
                auto& S = Scores[H.Hero]; S.Hero = H.Hero; S.Games += H.Games; S.Wins += H.Wins;
                S.LastPlayed = std::max(S.LastPlayed, H.LastPlayed);
                S.BanScore += H.Games / Total * ((H.Wins + 3.) / (H.Games + 6.)) * 100;
            }
        }
        std::vector<PlayerAnalysis::HeroUsage> Out; for (const auto& [Id, H] : Scores) Out.push_back(H);
        std::sort(Out.begin(), Out.end(), [](const auto& A, const auto& B) { return A.BanScore == B.BanScore ? A.Hero < B.Hero : A.BanScore > B.BanScore; });
        return Out;
    }
}
