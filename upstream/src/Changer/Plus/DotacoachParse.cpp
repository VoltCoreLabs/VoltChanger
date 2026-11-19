#include "DotacoachParse.h"
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
    using Value = google::protobuf::Value;
    constexpr size_t Limit = 5 * 1024 * 1024;
    [[noreturn]] void Fail(const char* What) { throw std::runtime_error(What); }
    const Value& Field(const Value& V, const char* Name)
    {
        static const Value Empty;
        if (V.kind_case() != Value::kStructValue) return Empty;
        const auto& F = V.struct_value().fields(); const auto I = F.find(Name);
        return I == F.end() ? Empty : I->second;
    }
    std::string Text(const Value& V) { return V.kind_case() == Value::kStringValue ? V.string_value() : std::string{}; }
    bool Identifier(const std::string& S, bool Slug = false)
    {
        return !S.empty() && S.size() < 160 && std::all_of(S.begin(), S.end(), [Slug](unsigned char C)
        { return (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_' || (Slug && C == '-'); });
    }
    // Both the JSON string and the JSON array scanners honor escaped quotes.
    size_t StringEnd(const std::string& S, size_t P)
    {
        if (P >= S.size() || S[P] != '"') Fail("DotaCoach malformed string");
        for (++P; P < S.size(); ++P)
        {
            if (S[P] == '\\') { if (++P >= S.size()) break; }
            else if (S[P] == '"') return P + 1;
        }
        Fail("DotaCoach truncated string");
    }
    Value Json(const std::string& S)
    {
        Value Out;
        if (!google::protobuf::util::JsonStringToMessage(S, &Out).ok()) Fail("DotaCoach malformed guide data");
        return Out;
    }
    std::string Flight(const std::string& Html)
    {
        if (Html.size() > Limit) Fail("DotaCoach response too large");
        const std::string Marker = "self.__next_f.push([1,";
        std::string Out; size_t P = 0;
        while ((P = Html.find(Marker, P)) != std::string::npos)
        {
            P += Marker.size();
            const auto E = StringEnd(Html, P);
            const auto V = Json(Html.substr(P, E - P));
            if (V.kind_case() != Value::kStringValue) Fail("DotaCoach unexpected page data");
            if (V.string_value().size() > Limit - Out.size()) Fail("DotaCoach decoded response too large");
            Out += V.string_value(); P = E;
        }
        if (Out.empty()) Fail("DotaCoach guide unavailable");
        return Out;
    }
    Value ArrayAt(const std::string& S, size_t P)
    {
        if (P >= S.size() || S[P] != '[') Fail("DotaCoach missing build array");
        const auto Start = P; int Depth = 0;
        for (; P < S.size(); ++P)
        {
            if (S[P] == '"') { P = StringEnd(S, P) - 1; continue; }
            if (S[P] == '[' || S[P] == '{') ++Depth;
            if (S[P] == ']' || S[P] == '}') if (!--Depth) return Json(S.substr(Start, P - Start + 1));
        }
        Fail("DotaCoach truncated guide array");
    }
    std::map<std::string, unsigned> Reverse(const ProTrackerData::Names& Names, const std::string& Prefix)
    {
        std::map<std::string, unsigned> Out;
        for (const auto& [Id, Name] : Names)
        {
            auto N = Name;
            if (!Prefix.empty() && N.compare(0, Prefix.size(), Prefix) == 0) N.erase(0, Prefix.size());
            if (Id && Identifier(N)) Out[N] = Id;
        }
        return Out;
    }
    // Source item metadata is authoritative; site URLs are not native item names.
    void ItemMetadata(const Value& V, std::map<std::string, std::string>& Out)
    {
        if (V.kind_case() == Value::kStructValue)
        {
            const auto Slug = Text(Field(V, "url_name")); auto Key = Text(Field(V, "key"));
            if (Identifier(Slug, true) && Key.compare(0, 5, "item_") == 0 && Identifier(Key.substr(5))) Out[Slug] = Key.substr(5);
            for (const auto& F : V.struct_value().fields()) ItemMetadata(F.second, Out);
        }
        else if (V.kind_case() == Value::kListValue)
            for (const auto& X : V.list_value().values()) ItemMetadata(X, Out);
    }
    void ImageMetadata(const std::string& Flight, std::map<std::string, std::string>& Out)
    {
        const std::string Marker = "\"itemUrlName\":", Image = "https://game.dotacoach.gg/vpk/panorama/images/items/";
        size_t P = 0;
        while ((P = Flight.find(Marker, P)) != std::string::npos)
        {
            P += Marker.size(); const auto E = StringEnd(Flight, P);
            const auto Slug = Text(Json(Flight.substr(P, E - P)));
            const auto I = Flight.find(Image, E);
            if (I != std::string::npos && I - E < 600)
            {
                const auto N = I + Image.size(), End = Flight.find(".webp", N);
                if (End != std::string::npos && End - N < 160)
                {
                    const auto Native = Flight.substr(N, End - N);
                    if (Identifier(Slug, true) && Identifier(Native)) Out.emplace(Slug, Native);
                }
            }
            P = E;
        }
    }
    std::map<unsigned, std::string> LevelBuild(const std::string& S)
    {
        std::map<unsigned, std::string> Out; std::istringstream Lines(S); std::string Line;
        while (std::getline(Lines, Line))
        {
            if (!Line.empty() && Line.back() == '\r') Line.pop_back();
            if (Line.empty()) continue;
            const auto H = Line.rfind('#'); if (H == std::string::npos) Fail("DotaCoach malformed skill level");
            auto Name = Line.substr(0, H); while (!Name.empty() && Name.back() == ' ') Name.pop_back();
            unsigned Level = 0; const auto R = std::from_chars(Line.data() + H + 1, Line.data() + Line.size(), Level);
            if (R.ec != std::errc() || R.ptr != Line.data() + Line.size() || Level < 1 || Level > 30) Fail("DotaCoach invalid skill level");
            if (Name.empty()) continue; // Source explicitly has unassigned levels, e.g. #23.
            if (!Identifier(Name) || !Out.emplace(Level, Name).second) Fail("DotaCoach invalid ability");
        }
        return Out;
    }
}

ProTrackerData::Snapshot DotacoachParse(const std::string& Html, const std::string& ExpectedSlug,
    unsigned Hero, unsigned Role, const ProTrackerData::Names& Items, const ProTrackerData::Names& Abilities)
{
    if (!Hero || Role < 1 || Role > 5 || !Identifier(ExpectedSlug, true)) Fail("DotaCoach invalid hero or role");
    const char* Roles[] = {"", "carry", "mid", "offlane", "support", "support"};
    auto F = Flight(Html);
    const std::string Marker = "\"heroUrlName\":\"" + ExpectedSlug + "\",\"heroBuilds\":";
    const auto P = F.find(Marker);
    if (P == std::string::npos) Fail("DotaCoach hero does not match request");
    const auto Builds = ArrayAt(F, P + Marker.size());
    const Value* Selected = nullptr;
    for (const auto& Build : Builds.list_value().values())
    {
        if (Text(Field(Build, "build_status")) != "active") continue;
        bool Match = false;
        for (const auto& R : Field(Build, "hero_roles_dota_coach").list_value().values())
            if (Text(Field(R, "name")) == Roles[Role]) Match = true;
        if (Match) { Selected = &Build; break; }
    }
    if (!Selected) Fail("DotaCoach has no guide for this role");
    std::map<std::string, std::string> Mapping; ItemMetadata(Builds, Mapping); ImageMetadata(F, Mapping);
    const auto NativeItems = Reverse(Items, "item_"), NativeAbilities = Reverse(Abilities, "");
    ProTrackerData::Snapshot Out; Out.Hero = Hero; Out.Role = Role; Out.Source = "DotaCoach";
    Out.SourceUrl = "https://dotacoach.gg/en/heroes/" + ExpectedSlug + "/builds/" + Roles[Role];
    const char* Stages[] = {"items_starting", "items_early_game", "items_mid_game", "items_late_game"};
    for (unsigned Stage = 0; Stage < 4; ++Stage)
    {
        const auto& Entries = Field(*Selected, Stages[Stage]);
        if (Entries.kind_case() != Value::kListValue || Entries.list_value().values_size() > 32) Fail("DotaCoach invalid item stage");
        for (const auto& Entry : Entries.list_value().values())
        {
            if (Field(Entry, "for_bear").bool_value()) continue;
            const auto Slug = Text(Field(Entry, "item_url_name")); const auto M = Mapping.find(Slug);
            if (M == Mapping.end()) Fail("DotaCoach item metadata unavailable");
            const auto I = NativeItems.find(M->second); if (I == NativeItems.end()) Fail("DotaCoach item absent from game metadata");
            ProTrackerData::Item Item; Item.Id = I->second; Item.Name = M->second;
            Out.Stages[Stage].push_back(std::move(Item));
        }
        if (Out.Stages[Stage].empty()) Fail("DotaCoach empty item stage");
    }
    for (const auto& [Level, Name] : LevelBuild(Text(Field(*Selected, "ability_build"))))
    {
        if (!NativeAbilities.count(Name)) Fail("DotaCoach ability absent from game metadata");
        Out.Skills.push_back({Level, Name});
    }
    for (const auto& [Level, Name] : LevelBuild(Text(Field(*Selected, "talent_build"))))
    {
        if ((Level != 10 && Level != 15 && Level != 20 && Level != 25) || !NativeAbilities.count(Name)) Fail("DotaCoach invalid talent");
        ProTrackerData::Talent T; T.Level = Level; T.Left = Name; Out.Talents.push_back(std::move(T));
    }
    if (Out.Skills.empty()) Fail("DotaCoach missing skill build");
    Out.HasBuild = true; Out.Status = Role >= 4 ? "DotaCoach: support guide (positions 4/5)" : "DotaCoach guide";
    return Out;
}
