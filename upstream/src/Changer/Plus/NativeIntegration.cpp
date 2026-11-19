#include "NativeIntegration.h"
#include "MetaData.h"
#include "BuildData.h"
#include "LocalPlus.h"
#include "PlayerAnalysis.h"
#include "PlayerSignals.h"
#include "ShopGuidePolicy.h"
#include "HeroBadgeSync.h"
#include "RecommendedShop.h"
#include "SDK/Panorama/Panel.h"
#include "SDK/Logger/Logger.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include <windows.h>
#include <algorithm>
#include <map>
#include <cmath>
#include <cstdio>
#include <charconv>
#include <set>

namespace
{
    ULONGLONG Next = 0;
    bool Meta = true, NativeShop = true, SettingsOpen = false;
    bool BanMode = false;
    unsigned ManualBanAccount = 0;
    std::string BanMessage, BanInputError;
    std::vector<PlayerAnalysis::HeroUsage> BanCandidates;
    unsigned Rank = 0, Lane = 0;
    Panel ActiveGrids[2];
    ULONGLONG NextDiscovery[2]{};
    const char* Ranks[] = {"Все ранги", "Рекрут", "Страж", "Рыцарь", "Герой", "Легенда", "Властелин", "Божество", "Титан"};
    const char* Lanes[] = {"Все линии", "Безопасная", "Центр", "Сложная", "Лес / роум"};
    unsigned ShopHero = 0;
    bool ShopBusy = false;
    std::string ShopStatus;
    V8::Local Object(const char* Name) { return V8::Get(Panorama::Isolate(), Panorama::Global(), Panorama::Context(), Name); }
    std::string Style(const Panel& P, const char* Key)
    {
        if (!P.Handle()) return {};
        auto S = V8::Get(Panorama::Isolate(), P.Handle(), Panorama::Context(), "style");
        auto V = S ? V8::Get(Panorama::Isolate(), S, Panorama::Context(), Key) : nullptr;
        return V && V8::TypeOf(Panorama::Isolate(), V) == "string" ? V8::ToString(Panorama::Isolate(), V) : std::string();
    }
    void Label(const Panel& P, const char* Id, const std::string& Value, const char* Size = "13px")
    {
        auto L = Panel::Create("Label", P, Id); L.SetBool("hittest", false);
        L.SetStyle({{"font-family", "Radiance"}, {"font-size", Size}, {"color", "#cbd7de"}, {"white-space", "normal"}}); L.SetText(Value.c_str()); L.Release();
    }
    void SetText(const Panel& P, const char* Id, const std::string& Value)
    { auto L = P.FindChildTraverse(Id); L.SetText(Value.c_str()); L.Release(); }
    std::string Percent(double N) { if (N < 0 || !std::isfinite(N)) return "—"; char B[32]; std::snprintf(B, sizeof(B), "%.1f%%", N); return B; }
    void Button(const Panel& P, const char* Id, const std::string& Caption, Panorama::Handler Callback, std::intptr_t Argument = 0)
    {
        auto B = Panel::Create("Button", P, Id); B.AddClass("ButtonPrimary"); B.AddClass("DarkGrey");
        B.SetBool("hittest", true); B.SetBool("hittestchildren", false);
        B.SetStyle({{"width", "fit-children"}, {"height", "30px"}, {"padding", "4px 10px"}, {"margin-right", "7px"}});
        Label(B, (std::string(Id) + "Text").c_str(), Caption); B.SetEvent("onactivate", Callback, Argument); B.Release();
    }
    void Toggle(std::intptr_t) { if (BanMode) { BanMode = false; Meta = true; } else Meta = !Meta; Next = 0; if (Meta) MetaData::Request(); }
    void Settings(std::intptr_t) { SettingsOpen = !SettingsOpen; Next = 0; }
    void BanToggle(std::intptr_t) { BanMode = !BanMode; Meta = true; Next = 0; MetaData::Request(); }
    void UseOpponents(std::intptr_t) { ManualBanAccount = 0; BanInputError.clear(); Next = 0; }
    void BanAccount(std::intptr_t Slot)
    {
        if (Slot < 0 || Slot > 1) return;
        auto Entry = ActiveGrids[Slot].FindChildTraverse("OvrNativeBanAccount");
        auto V = Entry.Handle() ? V8::Get(Panorama::Isolate(), Entry.Handle(), Panorama::Context(), "text") : nullptr;
        std::string Text = V ? V8::ToString(Panorama::Isolate(), V) : ""; Entry.Release();
        const auto Begin = Text.find_first_not_of(" \t\r\n"), End = Text.find_last_not_of(" \t\r\n");
        Text = Begin == std::string::npos ? "" : Text.substr(Begin, End - Begin + 1);
        unsigned Account = 0; auto Parsed = std::from_chars(Text.data(), Text.data() + Text.size(), Account);
        if (Account && Parsed.ec == std::errc() && Parsed.ptr == Text.data() + Text.size()) { ManualBanAccount = Account; BanInputError.clear(); PlayerAnalysis::Request(Account, false); }
        else BanInputError = "Введи числовой Steam account ID (32 бита).";
        Next = 0;
    }
    void RankNext(std::intptr_t) { Rank = (Rank + 1) % 9; Lane = 0; Next = 0; }
    void LaneNext(std::intptr_t) { Lane = (Lane + 1) % 5; if (Lane) Rank = 0; Next = 0; }
    void ShopToggle(std::intptr_t) { NativeShop = !NativeShop; ShopHero = 0; Next = 0; }
    unsigned CardHero(const Panel& Card)
    {
        auto Image = Card.FindChildTraverse("HeroImage"); unsigned Hero = 0;
        if (auto H = Image.Handle())
        {
            auto V = V8::Get(Panorama::Isolate(), H, Panorama::Context(), "heroid");
            if (V && V8::TypeOf(Panorama::Isolate(), V) == "number")
            {
                const auto N = V8::NumberValue(V, Panorama::Context()); if (N > 0 && N < 65536 && std::floor(N) == N) Hero = static_cast<unsigned>(N);
            }
            if (!Hero)
            {
                auto Name = V8::Get(Panorama::Isolate(), H, Panorama::Context(), "heroname");
                if (Name && V8::TypeOf(Panorama::Isolate(), Name) == "string")
                {
                    const auto Internal = V8::ToString(Panorama::Isolate(), Name); const auto Meta = MetaData::Get();
                    auto Found = std::find_if(Meta.Heroes.begin(), Meta.Heroes.end(), [&](const auto& H) { return H.Internal == Internal; });
                    if (Found != Meta.Heroes.end()) Hero = Found->Id;
                }
            }
        }
        Image.Release();
        if (!Hero) for (const char* Key : {"heroid", "hero_id", "HeroID"})
        {
            V8::Local Args[] = {Panorama::String(Key), Panorama::Number(0)};
            auto V = Panorama::Invoke(Card.Handle(), "GetAttributeInt", 2, Args);
            if (V && V8::TypeOf(Panorama::Isolate(), V) == "number")
            {
                const auto N = V8::NumberValue(V, Panorama::Context());
                if (N > 0 && N < 65536 && std::floor(N) == N) { Hero = static_cast<unsigned>(N); break; }
            }
        }
        return Hero;
    }
    void Grid(const Panel& Grid, const MetaData::Snapshot& Data, unsigned Slot)
    {
        // Native Dota owns categories, HeroList, card geometry and all input.
        // Only add non-interactive labels to the existing card contents.
        const bool Editing = Grid.HasClass("IsEditingHeroGrid");
        auto Footer = Grid.FindChild("Footer");
        auto Controls = Footer.FindChild("OvrNativeGridControls");
        if (Footer.IsValid() && !Controls.IsValid())
        {
            Controls.Release(); Controls = Panel::Create("Panel", Footer, "OvrNativeGridControls");
            Controls.SetStyle({{"ignore-parent-flow", "true"}, {"width", "fit-children"}, {"height", "30px"}, {"flow-children", "right"}, {"horizontal-align", "center"}, {"vertical-align", "center"}});
            Button(Controls, "OvrNativeMetaToggle", "WR / PR", Toggle);
            Button(Controls, "OvrNativeMetaSettings", "Статистика", Settings);
        }
        Controls.SetVisible(!Editing);
        SetText(Controls, "OvrNativeMetaToggleText", BanMode ? "Баны: подписи" : Meta ? "WR / PR: вкл." : "WR / PR: выкл.");
        Controls.Release(); Footer.Release();

        auto Options = Grid.FindChild("OvrNativeStatsOptions");
        if (SettingsOpen && !Options.IsValid())
        {
            Options.Release(); Options = Panel::Create("Panel", Grid, "OvrNativeStatsOptions");
            Options.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "center"}, {"vertical-align", "bottom"}, {"margin-bottom", "68px"}, {"width", "460px"}, {"height", "fit-children"}, {"flow-children", "down"}, {"padding", "12px"}, {"background-color", "#182128fc"}, {"border", "1px solid #61737b"}, {"z-index", "20"}});
            Options.SetBool("hittest", true); Options.SetBool("hittestchildren", true);
            Label(Options, "OvrNativeStatsHeading", "Подписи на штатных карточках Dota", "16px");
            auto Slice = Panel::Create("Panel", Options, "OvrNativeStatsSlice"); Slice.SetStyle({{"width", "100%"}, {"flow-children", "right"}});
            Button(Slice, "OvrNativeMetaRank", Ranks[Rank], RankNext); Button(Slice, "OvrNativeMetaLane", Lanes[Lane], LaneNext); Slice.Release();
            Button(Options, "OvrNativeBansToggle", "Показывать приоритет бана", BanToggle);
            auto Accounts = Panel::Create("Panel", Options, "OvrNativeBanAccounts"); Accounts.SetStyle({{"width", "100%"}, {"flow-children", "right"}});
            auto Entry = Panel::Create("TextEntry", Accounts, "OvrNativeBanAccount"); Entry.SetStyle({{"width", "130px"}, {"height", "29px"}, {"margin-right", "6px"}, {"background-color", "#30414c"}, {"color", "#ffffff"}}); Entry.SetBool("hittest", true); Entry.Release();
            Button(Accounts, "OvrNativeBanAnalyze", "По account ID", BanAccount, Slot); Accounts.Release();
            Button(Options, "OvrNativeBanOpponents", "Соперники матча", UseOpponents);
            Label(Options, "OvrNativeStatsSource", "", "12px");
            Label(Options, "OvrNativeStatsLimit", "Расположение, поиск, фильтры и выбор героя управляются Dota. Срез статистики меняет только цифры. N — число игр в выборке; PR≈ — оценка по 10 пикам на матч.", "12px");
            Button(Options, "OvrNativeStatsClose", "Закрыть", Settings);
        }
        Options.SetVisible(SettingsOpen && !Editing);
        SetText(Options, "OvrNativeMetaRankText", Ranks[Rank]); SetText(Options, "OvrNativeMetaLaneText", Lanes[Lane]);
        SetText(Options, "OvrNativeBansToggleText", BanMode ? "Баны: включены" : "Показывать приоритет бана");
        for (const char* Id : {"OvrNativeBanAccounts", "OvrNativeBanOpponents"}) { auto C = Options.FindChild(Id); C.SetVisible(BanMode); C.Release(); }
        auto Slice = Options.FindChild("OvrNativeStatsSlice"); Slice.SetVisible(!BanMode); Slice.Release();
        SetText(Options, "OvrNativeStatsSource", BanMode ? (BanInputError.empty() ? BanMessage : BanInputError) : Lane ? Data.LaneStatus + " PR по линии недоступен." : Data.Status);
        Options.Release();

        if (Meta) MetaData::Request();
        std::map<unsigned, const MetaData::Hero*> Heroes;
        for (const auto& H : Data.Heroes) Heroes.emplace(H.Id, &H);
        std::map<unsigned, const PlayerAnalysis::HeroUsage*> Bans;
        for (const auto& H : BanCandidates) Bans.emplace(H.Hero, &H);
        auto Cards = Grid.FindChildrenWithClass("HeroCard");
        unsigned Added = 0;
        for (auto& Card : Cards)
        {
            // Ignore editor add/drop placeholders; never instantiate native snippets.
            const auto Hero = CardHero(Card);
            HeroBadgeSync::Card(Card, Hero, Editing);
            auto Contents = Card.FindChildrenWithClass("HeroCardContents");
            if (Hero && !Contents.empty())
            {
                auto Stats = Contents.front().FindChild("OvrNativeRates");
                const bool Show = Meta && !Editing && (!BanMode || Bans.count(Hero));
                if (Show && !Stats.IsValid())
                {
                    Stats.Release(); Stats = Panel::Create("Label", Contents.front(), "OvrNativeRates");
                    Stats.SetBool("hittest", false); Stats.SetBool("hittestchildren", false);
                    Stats.SetStyle({{"ignore-parent-flow", "true"}, {"vertical-align", "top"}, {"horizontal-align", "left"}, {"margin-top", "5px"}, {"margin-left", "4px"}, {"width", "43px"}, {"height", "fit-children"}, {"padding", "1px 2px"}, {"font-family", "Radiance"}, {"font-size", "9px"}, {"line-height", "10px"}, {"white-space", "normal"}, {"color", "#edf1e7"}, {"background-color", "#101820dc"}, {"text-shadow", "0px 1px 2px 2.0 #000000"}});
                    ++Added;
                }
                if (Show)
                {
                    std::string Rates = "WR —\nPR≈ —";
                    if (BanMode)
                    {
                        const auto& H = *Bans.at(Hero); char Score[24]; std::snprintf(Score, sizeof(Score), "%.1f", H.BanScore);
                        Rates = "Бан " + std::string(Score) + "\nWR " + Percent(H.Games ? H.Wins * 100. / H.Games : -1) + "\nN " + std::to_string(H.Games);
                    }
                    else if (const auto It = Heroes.find(Hero); It != Heroes.end())
                    {
                        const auto& H = *It->second; const auto& C = Lane ? H.Lanes[Lane] : H.Ranks[Rank];
                        Rates = "WR " + Percent(C.WinRate()) + "\nPR≈ " + Percent(Lane ? -1 : MetaData::PickRate(Data.Heroes, H, Rank)) + "\nN " + std::to_string(static_cast<std::uint64_t>(C.Picks));
                    }
                    auto Current = V8::Get(Panorama::Isolate(), Stats.Handle(), Panorama::Context(), "text");
                    if (!Current || V8::ToString(Panorama::Isolate(), Current) != Rates) Stats.SetText(Rates.c_str());
                }
                Stats.SetVisible(Show); Stats.Release();
            }
            else
            {
                auto Previous = Card.FindChildTraverse("OvrNativeRates"); Previous.SetVisible(false); Previous.Release();
            }
            for (auto& P : Contents) P.Release();
            Card.Release();
        }
        if (Added) Log("Native hero cards annotated: added=%u slot=%u (original grid preserved)", Added, Slot);
    }

    void ReplacePlusContents(const Panel& Contents, bool Replace)
    {
        // Save only styles we change, on the actual native child. Selection is
        // read from the native PlusGuide class, never from these styles.
        auto Children = Contents.Children();
        for (auto& C : Children)
        {
            if (C.Id() != "OvrNativeBuildCategories" && C.Id() != "OvrD2ptBuilds" && !C.HasClass("Title"))
            {
                auto Saved = V8::Get(Panorama::Isolate(), C.Handle(), Panorama::Context(), "__ovrPlusGuideVisibility");
                const bool Captured = Saved && V8::TypeOf(Panorama::Isolate(), Saved) == "string";
                if (Replace)
                {
                    if (!Captured) V8::Set(C.Handle(), Panorama::Context(), Panorama::String("__ovrPlusGuideVisibility"), Panorama::String(Style(C, "visibility").c_str()));
                    C.SetVisible(false);
                }
                else if (Captured)
                {
                    C.SetStyle("visibility", V8::ToString(Panorama::Isolate(), Saved).c_str());
                    V8::Set(C.Handle(), Panorama::Context(), Panorama::String("__ovrPlusGuideVisibility"), Panorama::Number(0));
                }
            }
            C.Release();
        }
    }
    void Shop()
    {
        Panorama::Scope Scope(Panorama::RootKind::Hud); if (!Scope.IsValid()) return;
        auto Root = Panel::Root(), Shop = Root.FindChildTraverse("shop"); Root.Release(); if (!Shop.IsValid()) { Shop.Release(); return; }
        auto* CurrentHero = C_DOTAPlayerController::LocalHero();
        if (RecommendedShop::Update(Shop, CurrentHero ? CurrentHero->HeroId() : 0)) { Shop.Release(); return; }
        auto Plus = Shop.FindChildTraverse("ItemPlusGuide");
        auto ContentPanels = Plus.FindChildrenWithClass("PlusGuideContents");
        Panel Contents;
        if (ContentPanels.size() == 1) std::swap(Contents, ContentPanels.front());
        for (auto& P : ContentPanels) P.Release();
        if (!Plus.IsValid() || !Contents.IsValid()) { Contents.Release(); Plus.Release(); Shop.Release(); return; }
        // Dota's CSS routes PlusGuide to ItemPlusGuide, and every other guide
        // (including Workshop selections) to ItemBuild. Never override either
        // parent's visibility or change a guide ID/browser event.
        auto* Hero = C_DOTAPlayerController::LocalHero();
        const bool Enabled = ShopGuidePolicy::UseOpenDota(NativeShop, Hero != nullptr, Shop.HasClass("PlusGuide"));
        auto Own = Contents.FindChild("OvrNativeBuildCategories");
        ReplacePlusContents(Contents, Enabled);
        if (!Enabled)
        {
            Own.SetVisible(false);
            Own.Release(); Contents.Release(); Plus.Release(); Shop.Release(); return;
        }
        BuildData::Request(Hero->HeroId()); const auto Data = BuildData::Get(Hero->HeroId());
        if (!Own.IsValid() || ShopHero != Hero->HeroId() || ShopBusy != Data.Busy || ShopStatus != Data.ItemsStatus)
        {
            if (Own.IsValid()) Own.Delete(); Own.Release(); Own = Panel::Create("Panel", Contents, "OvrNativeBuildCategories");
            Own.SetStyle({{"width", "100%"}, {"flow-children", "down"}, {"overflow", "squish scroll"}});
            Label(Own, "OvrNativeShopSource", "OpenDota · цифры на значках — покупки в выборке", "13px");
            Label(Own, "OvrNativeShopStatus", Data.ItemsStatus, "11px");
            const char* Names[] = {"Стартовые предметы", "Ранняя игра", "Середина игры", "Поздняя игра"};
            const std::vector<AssistantData::Item>* Stages[] = {&Data.StartingItems, &Data.EarlyItems, &Data.MidItems, &Data.LateItems};
            for (unsigned Phase = 0; Phase < 4; ++Phase)
            {
                auto C = Panel::Create("Panel", Own, ("OvrNativeStage" + std::to_string(Phase)).c_str());
                C.SetStyle({{"width", "100%"}, {"flow-children", "down"}});
                C.AddClass("ItemBuildCategory");
                Label(C, "CategoryName", Names[Phase], "15px");
                auto Items = C.FindChildTraverse("ItemList"); if (!Items.IsValid()) { Items.Release(); Items = Panel::Create("Panel", C, "ItemList"); }
                Items.SetStyle({{"width", "100%"}, {"flow-children", "right-wrap"}});
                unsigned I = 0;
                for (const auto& Item : *Stages[Phase])
                {
                    if (I++ >= 8) break;
                    auto Widget = Panel::CreateWithProperties("DOTAShopItem", Items, ("OvrShopItem" + std::to_string(Item.Id)).c_str(), {{"abilityid", std::to_string(Item.Id)}, {"draggable", "true"}});
                    Widget.SetStyle({{"width", "50px"}, {"height", "36px"}, {"margin-right", "2px"}, {"margin-bottom", "3px"}});
                    Label(Widget, "OvrNativeItemFrequency", std::to_string(static_cast<std::uint64_t>(Item.Frequency)), "9px");
                    auto Count = Widget.FindChild("OvrNativeItemFrequency"); Count.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "right"}, {"vertical-align", "top"}, {"background-color", "#000c"}, {"padding", "0px 2px"}, {"color", "#dfd2a9"}}); Count.Release(); Widget.Release();
                }
                Items.Release(); C.Release();
            }
            ShopHero = Hero->HeroId(); ShopBusy = Data.Busy; ShopStatus = Data.ItemsStatus;
            Log("Native shop categories populated: hero=%u early_items=%zu", ShopHero, Data.EarlyItems.size());
        }
        Own.SetVisible(true); Own.Release(); Contents.Release(); Plus.Release(); Shop.Release();
    }
    bool IsActiveGrid(const Panel& P)
    {
        if (!P.IsValid() || P.Type() != "DOTAHeroGridNew" || P.Number("actuallayoutwidth") <= 0 || P.Number("actuallayoutheight") <= 0) return false;
        auto Visible = V8::Get(Panorama::Isolate(), P.Handle(), Panorama::Context(), "visible");
        if (Visible && !V8::BooleanValue(Panorama::Isolate(), Visible)) return false;
        auto Ancestor = P.Parent();
        for (unsigned Depth = 0; Depth < 20 && Ancestor.IsValid(); ++Depth)
        {
            auto V = V8::Get(Panorama::Isolate(), Ancestor.Handle(), Panorama::Context(), "visible");
            if (V && !V8::BooleanValue(Panorama::Isolate(), V)) { Ancestor.Release(); return false; }
            auto Parent = Ancestor.Parent(); Ancestor.Release(); Ancestor = Parent;
        }
        Ancestor.Release(); return true;
    }
    void FindActiveGrid(const Panel& Root, unsigned Slot, const MetaData::Snapshot& Data)
    {
        auto& Cached = ActiveGrids[Slot];
        if (IsActiveGrid(Cached)) { Grid(Cached, Data, Slot); return; }
        if (!Cached.IsValid()) Cached.Release();
        auto Candidate = Root.FindChildTraverse("HeroGrid");
        if (IsActiveGrid(Candidate)) { std::swap(Cached, Candidate); Candidate.Release(); Grid(Cached, Data, Slot); return; }
        Candidate.Release();
        if (GetTickCount64() < NextDiscovery[Slot]) return;
        NextDiscovery[Slot] = GetTickCount64() + 3000;
        // A hidden menu grid may precede the draft/visible grid in the tree.
        // Find the actual owner of the existing native cards, then cache it.
        auto Cards = Root.FindChildrenWithClass("HeroCard"); bool Found = false;
        for (auto& Card : Cards)
        {
            if (!Found)
            {
                auto Parent = Card.FindAncestor("HeroGrid");
                if (!Parent.IsValid())
                {
                    Parent.Release(); Parent = Card.Parent();
                    for (unsigned Depth = 0; Depth < 8 && Parent.IsValid() && Parent.Type() != "DOTAHeroGridNew"; ++Depth)
                    {
                        auto NextParent = Parent.Parent(); Parent.Release(); Parent = NextParent;
                    }
                }
                if (IsActiveGrid(Parent)) { std::swap(Cached, Parent); Grid(Cached, Data, Slot); Found = true; }
                Parent.Release();
            }
            Card.Release();
        }
    }
    void CollectBans()
    {
        std::vector<unsigned> Accounts; std::set<unsigned> Picked;
        if (ManualBanAccount) Accounts.push_back(ManualBanAccount);
        else
        {
            auto CallNumber = [](const char* Owner, const char* Method, int Count = 0, V8::Local* Args = nullptr) {
                auto V = Panorama::Invoke(Object(Owner), Method, Count, Args);
                return V && V8::TypeOf(Panorama::Isolate(), V) == "number" ? V8::NumberValue(V, Panorama::Context()) : -1.;
            };
            const auto Local = CallNumber("Game", "GetLocalPlayerID"); V8::Local LocalArgs[] = {Panorama::Number(Local)};
            const auto Team = CallNumber("Players", "GetTeam", 1, LocalArgs);
            auto Players = Panorama::Invoke(Object("Game"), "GetAllPlayerIDs");
            if (Local >= 0 && (Team == 2 || Team == 3) && Players && V8::IsObject(Players))
                for (unsigned I = 0; I < std::min(24u, V8::ArrayLength(Players)); ++I)
                {
                    V8::Local Args[] = {V8::GetIndex(Players, Panorama::Context(), I)};
                    const auto Hero = CallNumber("Players", "GetSelectedHeroID", 1, Args);
                    if (Hero > 0 && Hero < 65536) Picked.insert(static_cast<unsigned>(Hero));
                    const auto OtherTeam = CallNumber("Players", "GetTeam", 1, Args);
                    if (OtherTeam == Team || (OtherTeam != 2 && OtherTeam != 3)) continue;
                    auto Info = Panorama::Invoke(Object("Game"), "GetPlayerInfo", 1, Args);
                    auto Steam = Info && V8::IsObject(Info) ? V8::Get(Panorama::Isolate(), Info, Panorama::Context(), "player_steamid") : nullptr;
                    if (!Steam || V8::TypeOf(Panorama::Isolate(), Steam) != "string") continue;
                    const auto Text = V8::ToString(Panorama::Isolate(), Steam); std::uint64_t Id = 0;
                    const auto Parsed = std::from_chars(Text.data(), Text.data() + Text.size(), Id);
                    constexpr std::uint64_t Base = 76561197960265728ull;
                    if (Parsed.ec == std::errc() && Parsed.ptr == Text.data() + Text.size() && Id > Base && Id <= Base + UINT32_MAX) Accounts.push_back(static_cast<unsigned>(Id - Base));
                }
        }
        std::sort(Accounts.begin(), Accounts.end()); Accounts.erase(std::unique(Accounts.begin(), Accounts.end()), Accounts.end());
        if (Accounts.size() > 5) Accounts.resize(5);
        std::vector<PlayerAnalysis::Snapshot> Sources; unsigned Ready = 0, Loading = 0;
        for (auto Id : Accounts)
        {
            PlayerAnalysis::Request(Id, false); auto Source = PlayerAnalysis::Get(Id);
            if (Source.Busy) ++Loading;
            if (!Source.RecentHeroes.empty() || !Source.Frequent.empty()) ++Ready;
            Sources.push_back(std::move(Source));
        }
        auto Candidates = PlayerSignals::Bans(Sources);
        std::erase_if(Candidates, [&](const auto& H) { return Picked.count(H.Hero) != 0; });
        std::string Message = Accounts.empty() ? "Клиент не предоставил публичные ID соперников. Скрытые идентификаторы не восстанавливаются; можно указать известный публичный account ID."
            : (ManualBanAccount ? "Выбран account ID " + std::to_string(ManualBanAccount) : "Соперники с публичными ID: " + std::to_string(Accounts.size()))
                + " · с данными " + std::to_string(Ready) + "/" + std::to_string(Accounts.size()) + " · загружается " + std::to_string(Loading)
                + ". Последние 30 доступных матчей, иначе публичные агрегаты. Приоритет — эвристика частоты и побед, не вероятность и не автоматический бан.";
        BanMessage = std::move(Message); BanCandidates = std::move(Candidates);
    }
}

void NativeIntegration::ToggleShop(std::intptr_t Value) { ShopToggle(Value); }
bool NativeIntegration::ShopEnabled() { return NativeShop; }
void NativeIntegration::AnalyzeBans(std::intptr_t Account)
{
    if (Account <= 0 || Account > UINT32_MAX) return;
    ManualBanAccount = static_cast<unsigned>(Account); BanMode = true; Meta = true; SettingsOpen = true;
    BanInputError.clear(); Next = 0; MetaData::Request(); PlayerAnalysis::Request(ManualBanAccount, false);
}

void NativeIntegration::Update()
{
    if (!LocalPlus::Enabled() || GetTickCount64() < Next) return;
    Next = GetTickCount64() + 700;
    if (BanMode) for (auto Kind : {Panorama::RootKind::Hud, Panorama::RootKind::Dashboard})
    {
        Panorama::Scope Scope(Kind); if (!Scope.IsValid()) continue;
        auto Root = Panel::Root(); const bool Valid = Root.IsValid(); Root.Release(); if (!Valid) continue;
        CollectBans(); break;
    }
    const auto Data = MetaData::Get();
    for (auto Kind : {Panorama::RootKind::Dashboard, Panorama::RootKind::Hud})
    {
        Panorama::Scope Scope(Kind); if (!Scope.IsValid()) continue;
        auto Root = Panel::Root(); if (!Root.IsValid()) { Root.Release(); continue; }
        FindActiveGrid(Root, static_cast<unsigned>(Kind), Data); Root.Release();
    }
    Shop();
}
