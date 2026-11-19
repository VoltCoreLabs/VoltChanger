#include "HistoryWire.h"
#include "ovr_plus.pb.h"
#include <algorithm>

bool HistoryWire::Decode(const std::string& Body, std::uint32_t Token, std::vector<BattleData::Match>& Matches)
{
    ovr::HistoryResponse Response;
    if (!Response.ParseFromString(Body) || !Response.has_request_id() || Response.request_id() != Token || Response.matches_size() > 100) return false;
    std::vector<BattleData::Match> Out;
    for (const auto& Row : Response.matches())
    {
        if (!Row.has_match_id() || !Row.match_id() || !Row.has_start_time() || !Row.has_hero_id() || Row.hero_id() <= 0 || Row.hero_id() > 65535 || !Row.has_winner()) continue;
        BattleData::Match M;
        M.Id = Row.match_id(); M.Started = Row.start_time(); M.Hero = Row.hero_id(); M.Won = Row.winner();
        M.Duration = Row.has_duration() ? Row.duration() : UINT32_MAX;
        Out.push_back(M);
    }
    std::sort(Out.begin(), Out.end(), [](const auto& A, const auto& B) { return A.Started > B.Started; });
    Matches = std::move(Out);
    return true;
}
