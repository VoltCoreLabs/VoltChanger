// Include this isolated implementation to exercise its private parsers without
// exposing JSON helpers in the game/UI-facing header. This target must not also
// compile AssistantData.cpp separately.
#include "../upstream/src/Changer/Plus/AssistantData.cpp"
#include <iostream>

namespace
{
    void Check(bool Value, const char* Message)
    {
        if (!Value)
            throw std::runtime_error(Message);
    }
    template<class Action>
    bool Throws(Action Run)
    {
        try { Run(); return false; }
        catch (const std::exception&) { return true; }
    }
}

int main()
{
    try
    {
        Names Items{{1, "blink"}, {2, "boots"}};
        AssistantData::Snapshot Out;
        auto Popularity = Parse(R"({"start_game_items":{"1":12,"2":30,"999":100},"early_game_items":{"1":5},"mid_game_items":{},"late_game_items":{"2":7}})");
        ParseItems(Popularity, Items, Out);
        Check(Out.StartingItems.size() == 2 && Out.StartingItems[0].Name == "item_boots" && Out.StartingItems[0].Frequency == 30,
              "popularity must map names, preserve real counts and sort deterministically");
        Check(Out.StartingItems[1].Id == 1 && Out.MidItems.empty(), "unknown names must not be guessed");
        Check(Throws([&] { ParseItems(Parse("{}"), Items, Out); }), "missing stages must terminate with an error");

        ParseTimed(Parse(R"({"rank_chunked_stats":[{"rank_chunk":1,"timed_stats":[{"time":300,"all_stats":{"last_hits":999,"denies":1,"net_worth":2}}]},{"rank_chunk":0,"timed_stats":[{"time":600,"all_stats":{"last_hits":33.25,"denies":5.6,"net_worth":3237.6}},{"time":300,"all_stats":{"last_hits":15.1,"denies":3,"net_worth":1800}}]}]})"), Out);
        Check(Out.TimedStats.size() == 2 && Out.TimedStats.front().Time == 300 && Out.TimedStats.front().LastHits == 15.1,
              "timed data must select all-ranks only and sort by game time");
        Check(Throws([&] { ParseTimed(Parse(R"({"rank_chunked_stats":[]})"), Out); }), "missing rank data must fail instead of inventing targets");

        Out.HeroId = 1;
        Names Abilities{{5003, "antimage_mana_break"}, {5004, "antimage_blink"}};
        ParseSkills(Parse(R"({"players":[{"hero_id":2,"ability_upgrades_arr":[5004]},{"hero_id":1,"ability_upgrades_arr":[5003,5004,5003]}]})"), Abilities, Out);
        Check(Out.SkillOrder == std::vector<std::string>({"antimage_mana_break", "antimage_blink", "antimage_mana_break"}),
              "skill sequence must preserve exact requested hero match order");
        Check(Throws([&] { ParseSkills(Parse(R"({"players":[{"hero_id":1,"ability_upgrades_arr":[999999]}]})"), Abilities, Out); }),
              "unmapped skills must report missing data, never silently shift levels");
        Check(Throws([&] { ParseSkills(Parse(R"({"players":[{"hero_id":1}]})"), Abilities, Out); }), "unparsed matches must terminate spinner");
        Check(FirstMatch(Parse(R"([{"match_id":9091678507}])")) == 9091678507ull, "public match IDs must retain precision");
        Check(Throws([&] { FirstMatch(Parse("[]")); }), "empty public match list must terminate spinner");
        auto History = ParseHistory(Parse(R"([{"match_id":9091678507,"start_time":200,"hero_id":1,"duration":1801,"player_slot":128,"radiant_win":false,"kills":5,"deaths":2,"assists":9,"gold_per_min":500},{"match_id":2,"start_time":100,"hero_id":2,"duration":1200,"player_slot":0,"radiant_win":false},{"match_id":3,"start_time":300,"hero_id":2,"duration":1200,"player_slot":0,"radiant_win":null}])"));
        Check(History.size() == 2 && History[0].Id == 9091678507ull && History[0].Won && !History[1].Won,
              "history must preserve IDs, order and team-relative result; unfinished matches omitted");
        Check(History[0].Xpm == -1 && History[1].Kills == -1, "missing public stats must remain unavailable");
        Check(History[0].Lane == -1, "missing public lane must remain unavailable");
        const auto LaneHistory = ParseHistory(Parse(R"([{"match_id":1,"start_time":100,"hero_id":1,"duration":1200,"player_slot":0,"radiant_win":true,"lane_role":3}])"));
        Check(LaneHistory.size() == 1 && LaneHistory[0].Lane == 3, "public lane must survive into the per-match sample");
        Check(ParseHistory(Parse("[]")).empty(), "private or empty history must terminate without invented matches");
        Check(Throws([&] { ParseHistory(Parse(R"([{"match_id":1,"start_time":100,"hero_id":1,"duration":30,"player_slot":8,"radiant_win":true}])")); }), "invalid player slots must not produce fabricated results");
        Check(Throws([&] { ParseHistory(Parse("{}")); }), "error objects must not parse as match lists");
        auto Meta = ParseMeta(Parse(R"([{"id":1,"name":"npc_dota_hero_antimage","localized_name":"Anti-Mage","pub_pick":200,"pub_win":150,"1_pick":10,"1_win":8},{"id":2,"name":"npc_dota_hero_axe","localized_name":"Axe","pub_pick":100,"pub_win":50,"1_pick":0,"1_win":0}])"));
        Check(Meta.size() == 2 && Meta[0].Ranks[0].WinRate() == 75 && Meta[1].Ranks[1].WinRate() < 0, "win rate must use real counters and distinguish missing rank samples");
        ParseMetaLanes(Parse(R"([{"hero_id":1,"lane_role":1,"time":900,"games":"20","wins":"12"},{"hero_id":1,"lane_role":1,"time":1200,"games":"900","wins":"899"}])"), Meta);
        Check(Meta[0].Lanes[1].Picks == 20 && Meta[0].Lanes[1].WinRate() == 60, "lane time slices must not be double counted");
        MetaData::Snapshot MS; MS.Heroes = Meta;
        auto Sorted = MetaData::Sorted(MS, 0, 0, 0, false);
        Check(Sorted.size() == 2 && Sorted[0].Id == 1, "WR descending sort failed");
        Check(MetaData::Sorted(MS, 1, 0, 0, true).size() == 1 && MetaData::Sorted(MS, 0, 1, 0, true).size() == 1, "rank/lane filtering must exclude unavailable samples");
        Check(Throws([&] { ParseMeta(Parse(R"([{"id":1,"name":"a","localized_name":"A","pub_pick":10,"pub_win":11}])")); }), "wins above games must not create fabricated rates");
        std::vector<MetaData::Hero> Catalog(20);
        for (auto& H : Catalog) H.Ranks[0] = {100, 50};
        Check(MetaData::PickRate(Catalog, Catalog[0], 0) == 50, "PR must estimate games with ten picks, not share of all picked slots");
        MetaData::Detail Facets;
        AddFacetSample(Parse(R"({"radiant_win":false,"players":[{"hero_id":1,"hero_variant":2,"player_slot":128},{"hero_id":2,"hero_variant":1,"player_slot":0}]})"), 1, Facets);
        Check(Facets.ParsedMatches == 1 && Facets.Facets.size() == 1 && Facets.Facets[0].Variant == 2 && Facets.Facets[0].Stats.Wins == 1, "facet result must be tied to exact hero and player team");
        AddFacetSample(Parse(R"({"radiant_win":true,"players":[{"hero_id":1,"player_slot":0}]})"), 1, Facets);
        Check(Facets.ParsedMatches == 1, "missing hero_variant must not be invented");
        auto Analysis = ParsePlayerAnalysis(Parse("[]"), 7);
        Check(Analysis.Matches.empty() && Analysis.Role.find("lane_role") != std::string::npos, "private/empty history must not invent a player role");
        auto HeroTotals = ParsePlayerHeroes(Parse(R"([{"hero_id":1,"games":"20","win":"12","last_played":200},{"hero_id":2,"games":30,"win":14,"last_played":100},{"hero_id":3,"games":0,"win":0}])"));
        Check(HeroTotals.size() == 2 && HeroTotals[0].Hero == 2 && HeroTotals[1].Wins == 12 && HeroTotals[1].LastPlayed == 200, "public aggregates must preserve counts, timestamps and string numbers");
        Check(ParsePlayerHeroes(Parse("[]")).empty(), "unavailable aggregates must not invent hero picks");
        Check(Throws([&] { ParsePlayerHeroes(Parse(R"([{"hero_id":1,"games":3,"win":4}])")); }), "invalid aggregate wins must fail");
        Check(Throws([&] { ParsePlayerHeroes(Parse("{}")); }), "API error must not parse as empty successful aggregates");
        Check(Throws([&] { ParsePlayerHeroes(Parse(R"([{"hero_id":1,"games":3}])")); }), "missing win count must not become invented losses");
        std::cout << "Assistant, history, meta, lane, facet and player parser scenarios passed\n";
    }
    catch (const std::exception& Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
    return 0;
}
