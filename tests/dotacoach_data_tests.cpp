#include "Changer/Plus/DotacoachParse.h"
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
    void Check(bool OK, const char* Why) { if (!OK) throw std::runtime_error(Why); }
    std::string Read(const std::filesystem::path& Path)
    {
        std::ifstream F(Path, std::ios::binary); Check(bool(F), "fixture file missing");
        return std::string(std::istreambuf_iterator<char>(F), {});
    }
    ProTrackerData::Names Names(const std::filesystem::path& Path)
    {
        google::protobuf::Struct V;
        Check(google::protobuf::util::JsonStringToMessage(Read(Path), &V).ok(), "metadata JSON invalid");
        ProTrackerData::Names Out;
        for (const auto& [K, Value] : V.fields()) Out[std::stoul(K)] = Value.string_value();
        return Out;
    }
    template<class F> void Reject(F Action, const char* Why)
    { bool Failed = false; try { Action(); } catch (const std::exception&) { Failed = true; } Check(Failed, Why); }
}
int main(int Argc, char** Argv)
{
    try
    {
        const std::filesystem::path Root(OVR_SOURCE_ROOT);
        const auto Items = Names(Root / L"resources/assistant/item_ids.json");
        const auto Abilities = Names(Root / L"resources/assistant/ability_ids.json");
        const auto Fixture = [&Root](const char* Hero) { return Read(Root / L"tests/fixtures" / (std::string("dotacoach-") + Hero + ".html")); };
        const auto Tide = Fixture("tidehunter"), Abaddon = Fixture("abaddon"), Lina = Fixture("lina");
        const auto Parse = [&](const std::string& H, const std::string& Slug, unsigned Hero, unsigned Role)
        { return DotacoachParse(H, Slug, Hero, Role, Items, Abilities); };
        const auto T = Parse(Tide, "tidehunter", 29, 3);
        Check(T.HasBuild && !T.Statistical && T.Matches == 0 && T.WinRate == -1 && T.Source == "DotaCoach", "curated guide must not invent match counts or probabilities");
        const unsigned Expected[] = {6, 6, 4, 4};
        for (unsigned I = 0; I < 4; ++I)
        {
            Check(T.Stages[I].size() == Expected[I], "actual stage membership and duplicate purchases must survive");
            for (const auto& X : T.Stages[I]) Check(X.Id && X.Pick == -1 && X.Minute == -1, "guide recommendations have no fabricated buy rates or times");
        }
        Check(T.Stages[0][0].Name == "tango" && T.Stages[3][0].Name == "shivas_guard", "source URL slugs must map to actual game item names");
        Check(T.Skills.size() == 22 && T.Skills[0].Name == "tidehunter_gush" && T.Skills[0].Level == 1, "actual level order including attribute bonuses must survive");
        Check(T.Talents.size() == 4 && T.Talents[0].Left == "special_bonus_unique_tidehunter_5" && T.Talents[0].Right.empty(), "only the actual recommended talent may be shown");
        Reject([&] { Parse(Tide, "tidehunter", 29, 1); }, "absent role must not reuse an offlane guide");
        Reject([&] { Parse(Tide, "lina", 25, 3); }, "wrong hero page must fail");
        Reject([&] { Parse("<html>Cloudflare</html>", "tidehunter", 29, 3); }, "blocked page must fail");
        Reject([&] { Parse(Tide, "tidehunter", 29, 6); }, "invalid role must fail");
        Reject([&] { DotacoachParse(Tide, "tidehunter", 29, 3, {}, Abilities); }, "unknown native items must fail instead of silently dropping purchases");
        Reject([&] { Parse(Tide.substr(0, Tide.size()/3), "tidehunter", 29, 3); }, "truncated source must fail");
        auto Malformed = Tide; Malformed.replace(Malformed.find("self.__next_f.push([1,"), 21, "self.__next_f.push([1,\"\\q");
        Reject([&] { Parse(Malformed, "tidehunter", 29, 3); }, "invalid JSON escape must fail");
        const auto A4 = Parse(Abaddon, "abaddon", 102, 4), A5 = Parse(Abaddon, "abaddon", 102, 5), A3 = Parse(Abaddon, "abaddon", 102, 3);
        Check(A4.Stages[0].size() == 8 && A5.Stages[0].size() == 8 && A3.Stages[0].size() == 6, "combined support guide must be explicit while distinct offlane guide stays separate");
        Check(A4.Status.find("4/5") != std::string::npos, "combined support source must be disclosed");
        Check(Parse(Lina, "lina", 25, 1).Stages[0].size() == 3 && Parse(Lina, "lina", 25, 2).Stages[0].size() == 7 && Parse(Lina, "lina", 25, 4).Stages[0].size() == 7, "multiple source roles must select their own guide");
        if (Argc > 1)
        {
            const auto Full = Parse(Read(Root / std::filesystem::path(Argv[1])), "tidehunter", 29, 3);
            for (unsigned I = 0; I < 4; ++I)
            {
                Check(Full.Stages[I].size() == T.Stages[I].size(), "full captured page stage size differs from recorded factual subset");
                for (unsigned J = 0; J < T.Stages[I].size(); ++J)
                    Check(Full.Stages[I][J].Id == T.Stages[I][J].Id && Full.Stages[I][J].Name == T.Stages[I][J].Name, "full captured page item order differs from recorded factual subset");
            }
            Check(Full.Skills.size() == T.Skills.size() && Full.Talents.size() == T.Talents.size(), "full captured page skill/talent extraction differs");
            std::cout << "Full WinHTTP Tidehunter page: stages 6/6/4/4, 22 skill levels, 4 recommended talents\n";
        }
        std::cout << "DotaCoach captured guide items, role isolation, skills, talents and malformed input passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; return 1; }
}
