#include "ProTrackerData.h"
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace
{
    using V = google::protobuf::Value;
    const V* Field(const V& O, const char* Key)
    { if (O.kind_case() != V::kStructValue) return nullptr; const auto& F = O.struct_value().fields(); auto I = F.find(Key); return I == F.end() ? nullptr : &I->second; }
    const google::protobuf::RepeatedPtrField<V>& Array(const V* O)
    { static const google::protobuf::RepeatedPtrField<V> Empty; return O && O->kind_case() == V::kListValue ? O->list_value().values() : Empty; }
    double N(const V* O, double Default = -1)
    { return O && O->kind_case() == V::kNumberValue && std::isfinite(O->number_value()) ? O->number_value() : Default; }
    std::string S(const V* O) { return O && O->kind_case() == V::kStringValue ? O->string_value() : ""; }
    unsigned Id(const V* O)
    { const auto X = N(O); return X > 0 && X <= 65535 && X == std::floor(X) ? static_cast<unsigned>(X) : 0; }
    double Rate(const V* O) { const auto X = N(O); return X >= 0 && X <= 1 ? X * 100 : -1; }
    bool Name(const std::string& S)
    { return !S.empty() && S.size() <= 160 && S.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos; }
    std::string Ability(const V& O, const ProTrackerData::Names& Names)
    {
        auto Text = S(Field(O, "name")); if (Name(Text)) return Text;
        const auto Found = Names.find(Id(Field(O, "ability_id"))); return Found == Names.end() ? "" : Found->second;
    }
}
ProTrackerData::Snapshot ProTrackerData::Parse(const std::string& Json, unsigned Hero, unsigned Role, const Names& Items, const Names& Abilities)
{
    if (!Hero || Hero > 65535 || Role < 1 || Role > 5 || Json.size() > 5 * 1024 * 1024) throw std::runtime_error("Invalid D2PT request");
    V Root; if (!google::protobuf::util::JsonStringToMessage(Json, &Root).ok() || Root.kind_case() != V::kListValue) throw std::runtime_error("Invalid D2PT JSON");
    const V* Best = nullptr; double BestCount = -1;
    for (const auto& Build : Array(&Root))
    {
        const auto Count = N(Field(Build, "num_matches"));
        if (Id(Field(Build, "hero_id")) == Hero && S(Field(Build, "position")) == "pos " + std::to_string(Role)
            && Count >= 1 && Count <= 100000000 && Count == std::floor(Count) && Count > BestCount
            && Field(Build, "build_data") && Field(Build, "build_data")->kind_case() == V::kStructValue)
        { Best = &Build; BestCount = Count; }
    }
    Snapshot Out; Out.Hero = Hero; Out.Role = Role;
    if (!Best) { Out.Status = "Dota2ProTracker: нет сборок для этой позиции"; return Out; }
    Out.Matches = static_cast<unsigned>(BestCount); Out.Updated = S(Field(*Best, "updated_at"));
    Out.HasBuild = true; Out.Statistical = true; Out.Source = "Dota2ProTracker";
    const auto Wins = N(Field(*Best, "num_wins")); if (Wins >= 0 && Wins <= BestCount) Out.WinRate = Wins * 100 / BestCount;
    const auto& Data = *Field(*Best, "build_data");
    auto ReadItems = [&](const char* Key, bool Starting) {
        std::set<unsigned> Seen;
        for (const auto& I : Array(Field(Data, Key)))
        {
            const auto ItemId = Id(Field(I, "raw_item_id")); const auto Found = Items.find(ItemId);
            const auto Pick = Rate(Field(I, "pr")), Minute = N(Field(I, "avg_minute"));
            if (!ItemId || Found == Items.end() || !Name(Found->second) || Pick < 0 || !Seen.insert(ItemId).second) continue;
            if (!Starting && (Minute < 0 || Minute > 240)) continue;
            const auto Phase = Starting ? 0 : Minute < 12 ? 1 : Minute < 25 ? 2 : 3;
            Out.Stages[Phase].push_back({ItemId, Found->second, Pick, Starting ? -1 : Minute});
        }
    };
    ReadItems("starting_items", true); ReadItems("items_mid_late", false);
    if (Out.Stages[1].empty() && Out.Stages[2].empty() && Out.Stages[3].empty()) ReadItems("anchor_items", false);
    for (auto& Stage : Out.Stages)
    {
        std::stable_sort(Stage.begin(), Stage.end(), [](const auto& A, const auto& B) { return A.Pick > B.Pick; });
        if (Stage.size() > 12) Stage.resize(12);
    }
    unsigned Level = 1;
    for (const auto& A : Array(Field(Data, "abilities")))
    { const auto Text = Ability(A, Abilities); if (Level > 30) break; if (!Text.empty()) Out.Skills.push_back({Level, Text}); ++Level; }
    std::set<unsigned> TalentLevels;
    for (const auto& T : Array(Field(Data, "talents")))
    {
        const auto L = Id(Field(T, "lvl")); const auto* Left = Field(T, "left"), *Right = Field(T, "right");
        if ((L != 10 && L != 15 && L != 20 && L != 25) || !Left || !Right || !TalentLevels.insert(L).second) continue;
        auto A = Ability(*Left, Abilities), B = Ability(*Right, Abilities);
        if (!A.empty() && !B.empty()) Out.Talents.push_back({L, A, B, Rate(Field(T, "left_pr")), Rate(Field(T, "right_pr"))});
    }
    std::sort(Out.Talents.begin(), Out.Talents.end(), [](const auto& A, const auto& B) { return A.Level > B.Level; });
    Out.Status = "Dota2ProTracker · наиболее сыгранная сборка позиции · " + std::to_string(Out.Matches) + " матчей";
    return Out;
}
