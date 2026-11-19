#include "HeroBadgeSync.h"
#include "HeroProgress.h"
#include "HeroProgressPlan.h"
#include "LocalPlus.h"
#include "SDK/SteamAPI/ISteamUser.h"
#include <windows.h>
#include <cmath>
#include <string>

namespace
{
    std::string String(V8::Local V)
    { return V && V8::TypeOf(Panorama::Isolate(), V) == "string" ? V8::ToString(Panorama::Isolate(), V) : ""; }
    V8::Local Property(const Panel& P, const char* Key)
    { return P.Handle() ? V8::Get(Panorama::Isolate(), P.Handle(), Panorama::Context(), Key) : nullptr; }
    void Remember(const Panel& P, const char* Key, const std::string& Value)
    { if (P.Handle()) V8::Set(P.Handle(), Panorama::Context(), Panorama::String(Key), Panorama::String(Value.c_str())); }
    bool Stored(const Panel& P, const char* Key) { auto V = Property(P, Key); return V && V8::TypeOf(Panorama::Isolate(), V) == "string"; }
    std::string Style(const Panel& P, const char* Key)
    { auto S = Property(P, "style"); return S ? String(V8::Get(Panorama::Isolate(), S, Panorama::Context(), Key)) : ""; }
    void OverrideStyle(const Panel& P, const char* Key, const char* Value, bool Active, const char* Fallback)
    {
        if (!P.IsValid()) return;
        const auto Save = std::string("__ovrTierStyle_") + Key;
        if (Active)
        {
            if (!Stored(P, Save.c_str())) Remember(P, Save.c_str(), Style(P, Key));
            P.SetStyle(Key, Value);
        }
        else if (Stored(P, Save.c_str()))
        {
            auto Prior = String(Property(P, Save.c_str()));
            P.SetStyle(Key, Prior.empty() ? Fallback : Prior.c_str()); P.SetBool(Save.c_str(), false);
        }
    }
    void OverrideText(const Panel& P, const std::string& Value, bool Active)
    {
        if (!P.IsValid()) return;
        constexpr const char* Key = "__ovrTierOriginalText";
        if (Active)
        {
            if (!Stored(P, Key)) Remember(P, Key, String(Property(P, "text")));
            if (String(Property(P, "text")) != Value) P.SetText(Value.c_str());
        }
        else if (Stored(P, Key)) { P.SetText(String(Property(P, Key)).c_str()); P.SetBool(Key, false); }
    }
    unsigned Numeric(const Panel& P, const char* Key)
    {
        auto V = Property(P, Key);
        if (!V || V8::TypeOf(Panorama::Isolate(), V) != "number") return 0;
        const auto N = V8::NumberValue(V, Panorama::Context());
        return std::isfinite(N) && N > 0 && N <= UINT32_MAX && N == std::floor(N) ? static_cast<unsigned>(N) : 0;
    }
    unsigned Hero(const Panel& P)
    {
        for (const auto* Key : {"heroid", "hero_id", "HeroID"})
        {
            const auto Id = Numeric(P, Key); if (Id && Id < 65536) return Id;
        }
        auto Image = P.FindChild("HeroImage"); const auto Id = Numeric(Image, "heroid"); Image.Release();
        return Id < 65536 ? Id : 0;
    }
    void ApplyBadge(const Panel& Badge, unsigned HeroId)
    {
        auto PagePresentation = Badge.FindChild("OvrHeroTierContents"); const bool PageOwned = PagePresentation.IsValid(); PagePresentation.Release();
        if (PageOwned) return; // the full hero page already has its verified renderer
        const auto Level = HeroProgress::Level(HeroId);
        auto Local = Badge.FindChild("OvrSharedHeroTier");
        if (!Level && !Local.IsValid()) { Local.Release(); return; }
        if (Level && !Local.IsValid())
        {
            Local.Release(); Local = Panel::Create("Panel", Badge, "OvrSharedHeroTier");
            Local.SetBool("hittest", false); Local.SetBool("hittestchildren", false);
            const char* Size = Badge.HasClass("ModelStyle") ? "112px" : "100%";
            Local.SetStyle({{"width", Size}, {"height", Size}, {"horizontal-align", "center"}, {"vertical-align", "center"}});
            auto Image = Panel::Create("Image", Local, "Icon"); Image.SetBool("hittest", false);
            Image.SetStyle({{"width", "100%"}, {"height", "100%"}}); Image.Release();
            auto Number = Panel::Create("Label", Local, "Level"); Number.SetBool("hittest", false);
            Number.SetStyle({{"horizontal-align", "center"}, {"vertical-align", "center"}, {"font-family", "Radiance"}, {"font-size", Badge.HasClass("IconStyle") ? "16px" : "29px"},
                {"font-weight", "bold"}, {"color", "white"}, {"text-shadow", "0px 2px 4px 3 #000c"}}); Number.Release();
        }
        const bool Active = Level && Local.IsValid();
        OverrideStyle(Badge, "visibility", "visible", Active, Badge.HasClass("NoTier") ? "collapse" : "visible");
        auto Parts = Badge.Children();
        for (auto& P : Parts)
        { if (P.Id() != "OvrSharedHeroTier") OverrideStyle(P, "visibility", "collapse", Active, "visible"); P.Release(); }
        Local.SetVisible(Active);
        if (Active)
        {
            auto Image = Local.FindChild("Icon");
            Image.SetImage(("s2r://panorama/images/hero_badges/hero_badge_rank_" + std::to_string(HeroProgressPlan::Index(Level)) + "_png.vtex").c_str()); Image.Release();
            auto Number = Local.FindChild("Level"); OverrideText(Number, std::to_string(Level), true); Number.Release();
        }
        Local.Release();
    }
    void ContextBadge(const Panel& Badge)
    {
        auto* User = ISteamUser::Get(); const unsigned Own = User ? User->AccountId() : 0;
        if (!Own) return;
        auto Current = Badge.Parent();
        unsigned HeroId = Hero(Badge), Account = Numeric(Badge, "accountid"); bool Personal = false;
        for (unsigned Depth = 0; Current.IsValid() && Depth < 5; ++Depth)
        {
            if (!Account) Account = Numeric(Current, "accountid");
            if (Account && Account != Own) break;
            if (!HeroId) HeroId = Hero(Current);
            if (Current.HasClass("HeroCardTooltipContents") || Current.HasClass("HeroCard"))
            {
                if (!HeroId)
                {
                    auto Image = Current.FindChildTraverse("HeroImage"); HeroId = Numeric(Image, "heroid"); Image.Release();
                }
                Personal = true; break;
            }
            auto Parent = Current.Parent(); Current.Release(); Current = Parent;
        }
        Current.Release();
        if (HeroId && (Personal || Account == Own) && (!Account || Account == Own))
            ApplyBadge(Badge, HeroId);
        else ApplyBadge(Badge, 0); // recycled controls must not retain our badge on another player's profile
    }
}

void HeroBadgeSync::Card(const Panel& Card, unsigned HeroId, bool Editing)
{
    const auto Level = HeroProgress::Level(HeroId); const bool Active = Level && !Editing;
    constexpr const char* Marker = "__ovrTierCardOverride";
    if (!Level && !Stored(Card, Marker)) return;
    if (Active) Remember(Card, Marker, std::to_string(HeroId));
    auto Status = Card.FindChildTraverse("HeroBadgeStatus");
    auto Number = Status.FindChild("HeroBadgeLevel"), Icon = Status.FindChild("HeroBadgeIcon");
    const auto Image = "url(\"s2r://panorama/images/hero_badges/hero_badge_rank_" + std::to_string(HeroProgressPlan::Index(Level)) + "_png.vtex\")";
    OverrideText(Number, std::to_string(Level), Active);
    OverrideStyle(Number, "color", HeroProgressPlan::Color(Level), Active, "white");
    const char* Classes[] = {"BronzeTier", "SilverTier", "GoldTier", "PlatinumTier", "MasterTier", "GrandmasterTier"};
    unsigned NativeRank = 0;
    for (unsigned I = 0; I < 6; ++I) if (Card.HasClass(Classes[I])) NativeRank = I;
    const auto NativeImage = "url(\"s2r://panorama/images/hero_badges/hero_badge_rank_" + std::to_string(NativeRank) + "_png.vtex\")";
    OverrideStyle(Icon, "background-image", Image.c_str(), Active, NativeImage.c_str());
    OverrideStyle(Status, "visibility", "visible", Active, Card.HasClass("NoTier") ? "collapse" : "visible");
    Icon.Release(); Number.Release(); Status.Release();
    if (!Level) Card.SetBool(Marker, false);
}
void HeroBadgeSync::Update()
{
    static ULONGLONG Next = 0;
    if (!LocalPlus::Enabled() || GetTickCount64() < Next) return;
    Next = GetTickCount64() + 700;
    for (const auto Kind : {Panorama::RootKind::Dashboard, Panorama::RootKind::Hud})
    {
        Panorama::Scope Scope(Kind); if (!Scope.IsValid()) continue;
        auto Root = Panel::Root();
        for (const auto* Class : {"IconStyle", "LevelStyle", "ModelStyle"})
        {
            auto Badges = Root.FindChildrenWithClass(Class);
            for (auto& P : Badges) { if (P.Type() == "DOTAHeroBadge") ContextBadge(P); P.Release(); }
        }
        Root.Release();
    }
}
