#include "MatchPlayersPanel.h"
#include "MatchPlayerSummary.h"
#include "MatchPanelPlan.h"
#include "MetaData.h"
#include "LocalPlus.h"
#include "SDK/Panorama/Panel.h"
#include "SDK/GameRules/C_DOTAGamerules.h"
#include "SDK/Logger/Logger.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

namespace
{
    struct Player
    {
        int Slot = -1, Team = 0, Kills = -1, Deaths = -1, Assists = -1;
        unsigned Account = 0, Hero = 0;
        std::string Name;
    };
    std::vector<Player> Players;
    Panel Scoreboard;
    std::uintptr_t HudRoot = 0;
    ULONGLONG Next = 0;
    std::uint64_t Painted = 0, ResultsPainted = 0;
    std::uint64_t LoggedRoster = 0;
    bool Open = false, Dirty = true, ResultsOpen = false;
    bool PickOpen = true;
    std::uint64_t PickPainted = 0;
    int Detail = -1;
    EGameState PreviousState = EGameState::Unknown;

    V8::Local Object(const char* Name) { return V8::Get(Panorama::Isolate(), Panorama::Global(), Panorama::Context(), Name); }
    double Number(const char* Owner, const char* Method, V8::Local Arg = nullptr)
    {
        auto Result = Panorama::Invoke(Object(Owner), Method, Arg ? 1 : 0, Arg ? &Arg : nullptr);
        return Result && V8::TypeOf(Panorama::Isolate(), Result) == "number" ? V8::NumberValue(Result, Panorama::Context()) : -1;
    }
    int Stat(const char* Method, V8::Local Slot)
    {
        const auto Value = Number("Players", Method, Slot);
        return std::isfinite(Value) && Value >= 0 && Value < 1000000 && Value == std::floor(Value) ? static_cast<int>(Value) : -1;
    }
    std::string Value(int N) { return N < 0 ? "—" : std::to_string(N); }
    std::string Percent(double N) { if (!std::isfinite(N) || N < 0) return "—"; char B[32]; std::snprintf(B, sizeof(B), "%.1f%%", N); return B; }
    void Text(const Panel& Parent, const char* Id, const std::string& Caption, const char* Color = "#d8e0e2", const char* Size = "13px")
    {
        auto Label = Panel::Create("Label", Parent, Id); Label.SetBool("hittest", false);
        Label.SetStyle({{"width", "100%"}, {"font-family", "Radiance"}, {"font-size", Size}, {"color", Color}, {"white-space", "normal"}, {"margin-bottom", "4px"}});
        Label.SetText(Caption.c_str()); Label.Release();
    }
    void Button(const Panel& Parent, const char* Id, const std::string& Caption, Panorama::Handler Handler, std::intptr_t Arg = 0)
    {
        auto B = Panel::Create("Button", Parent, Id); B.SetBool("hittest", true); B.SetBool("hittestchildren", false);
        B.SetStyle({{"width", "fit-children"}, {"height", "30px"}, {"padding", "5px 9px"}, {"margin-right", "6px"}, {"background-color", "#30434b"}});
        Text(B, "Caption", Caption);
        // A percentage-width child inside fit-children creates a circular
        // width dependency: the postgame recovery button became a blank strip.
        auto Label = B.FindChild("Caption"); Label.SetStyle("width", "fit-children"); Label.Release();
        B.SetEvent("onactivate", Handler, Arg); B.Release();
    }
    void Toggle(std::intptr_t) { Open = !Open; Dirty = true; Next = 0; }
    void Inspect(std::intptr_t Slot) { Detail = Detail == Slot ? -1 : static_cast<int>(Slot); Dirty = true; Next = 0; }
    void Results(std::intptr_t) { ResultsOpen = !ResultsOpen; Dirty = true; Next = 0; }
    void TogglePick(std::intptr_t) { PickOpen = !PickOpen; Dirty = true; Next = 0; }
    std::string HeroName(unsigned Id, const MetaData::Snapshot& Catalog)
    {
        const auto It = std::find_if(Catalog.Heroes.begin(), Catalog.Heroes.end(), [&](const auto& H) { return H.Id == Id; });
        return It == Catalog.Heroes.end() ? Id ? "Герой #" + std::to_string(Id) : "Герой ещё не выбран" : It->Name;
    }
    bool ActiveMatch(EGameState State)
    {
        return State != EGameState::Unknown && State != EGameState::Init && State != EGameState::Disconnect;
    }
    std::uint64_t Hash(std::uint64_t Seed, std::uint64_t N) { return (Seed ^ N) * 1099511628211ull; }
    std::uint64_t Revision()
    {
        std::uint64_t Result = 1469598103934665603ull;
        for (const auto& P : Players)
        {
            Result = Hash(Result, P.Slot); Result = Hash(Result, P.Hero); Result = Hash(Result, P.Account);
            Result = Hash(Result, P.Kills + 1); Result = Hash(Result, P.Deaths + 1); Result = Hash(Result, P.Assists + 1);
            for (unsigned char C : P.Name) Result = Hash(Result, C);
            if (P.Account) Result = Hash(Result, PlayerAnalysis::Get(P.Account).Revision);
        }
        return Hash(Result, MetaData::Get().Revision);
    }
    void PollPlayers()
    {
        auto Ids = Panorama::Invoke(Object("Game"), "GetAllPlayerIDs");
        if (!Ids || !V8::IsObject(Ids)) return;
        std::vector<Player> Current;
        for (unsigned I = 0; I < std::min(24u, V8::ArrayLength(Ids)); ++I)
        {
            auto Slot = V8::GetIndex(Ids, Panorama::Context(), I);
            const auto Team = Number("Players", "GetTeam", Slot);
            if (Team != 2 && Team != 3) continue;
            const auto Id = Slot && V8::TypeOf(Panorama::Isolate(), Slot) == "number" ? V8::NumberValue(Slot, Panorama::Context()) : -1;
            if (Id < 0 || Id >= 24 || Id != std::floor(Id)) continue;
            Player P; P.Slot = static_cast<int>(Id); P.Team = static_cast<int>(Team);
            auto Info = Panorama::Invoke(Object("Game"), "GetPlayerInfo", 1, &Slot);
            if (Info && V8::IsObject(Info))
            {
                auto Name = V8::Get(Panorama::Isolate(), Info, Panorama::Context(), "player_name");
                if (Name && V8::TypeOf(Panorama::Isolate(), Name) == "string") P.Name = V8::ToString(Panorama::Isolate(), Name);
                auto Steam = V8::Get(Panorama::Isolate(), Info, Panorama::Context(), "player_steamid");
                if (Steam && V8::TypeOf(Panorama::Isolate(), Steam) == "string") P.Account = MatchPlayerSummary::PublicAccount(V8::ToString(Panorama::Isolate(), Steam));
            }
            if (P.Name.empty()) P.Name = "Игрок " + std::to_string(P.Slot + 1);
            const auto Hero = Number("Players", "GetSelectedHeroID", Slot);
            if (Hero > 0 && Hero < 65536 && Hero == std::floor(Hero)) P.Hero = static_cast<unsigned>(Hero);
            P.Kills = Stat("GetKills", Slot); P.Deaths = Stat("GetDeaths", Slot); P.Assists = Stat("GetAssists", Slot);
            if (P.Account) PlayerAnalysis::Request(P.Account, false);
            Current.push_back(std::move(P));
        }
        if (!Current.empty())
        {
            std::uint64_t Roster = 1469598103934665603ull; unsigned Public = 0;
            for (const auto& P : Current) { Roster = Hash(Hash(Roster, P.Slot), P.Account); if (P.Account) ++Public; }
            if (Roster != LoggedRoster)
            {
                LoggedRoster = Roster;
                Log("Match players: %zu client participants, %u public string SteamIDs; automatic public-history queue active", Current.size(), Public);
            }
            Players = std::move(Current);
        }
    }
    Panel& FindScoreboard(const Panel& Root)
    {
        const auto Key = reinterpret_cast<std::uintptr_t>(Root.Native());
        if (Key != HudRoot) { Scoreboard.Release(); HudRoot = Key; }
        if (Scoreboard.IsValid()) return Scoreboard;
        Scoreboard.Release();
        for (const char* Id : {"scoreboard", "Scoreboard", "HudScoreboard"})
        {
            auto Candidate = Root.FindChildTraverse(Id);
            if (Candidate.IsValid() && Candidate.Type() == "DOTAScoreboard") { std::swap(Candidate, Scoreboard); Candidate.Release(); return Scoreboard; }
            Candidate.Release();
        }
        // The compiled layout does not assign an ID to DOTAScoreboard itself.
        // Its team container remains addressable while the scoreboard is open.
        auto TeamRows = Root.FindChildTraverse("RadiantTeamContainer");
        auto Background = TeamRows.Parent(), Board = Background.Parent();
        if (Board.IsValid() && Board.Type() == "DOTAScoreboard") std::swap(Board, Scoreboard);
        Board.Release(); Background.Release(); TeamRows.Release();
        if (Scoreboard.IsValid()) return Scoreboard;
        // Resource defines the native closed-state class. Retain its control
        // across opening, when Dota removes that class, without changing it.
        auto Closed = Root.FindChildrenWithClass("ScoreboardClosed");
        for (auto& Candidate : Closed)
        {
            if (!Scoreboard.IsValid() && Candidate.Type() == "DOTAScoreboard") std::swap(Candidate, Scoreboard);
            Candidate.Release();
        }
        return Scoreboard;
    }
    void DrawPlayers(const Panel& Parent)
    {
        const auto Catalog = MetaData::Get();
        Text(Parent, "Title", "Игроки · последние 15 публичных матчей", "#d7c08b", "17px");
        Text(Parent, "Source", "OpenDota. PR ниже — доля пиков в этой выборке, не популярность героя во всей Dota. Пропуски возможны; позиции 4/5 не различаются.", "#a4b3b8", "11px");
        const char* Lanes[] = {"нет данных", "безопасная", "центр", "сложная", "лес / перемещения"};
        for (const auto& P : Players)
        {
            auto Row = Panel::Create("Panel", Parent, ("Player" + std::to_string(P.Slot)).c_str());
            Row.SetStyle({{"width", "100%"}, {"flow-children", "down"}, {"padding", "8px"}, {"margin-bottom", "6px"}, {"background-color", P.Team == 2 ? "#20342c" : "#36292b"}});
            Button(Row, "Name", P.Name + " · " + (P.Team == 2 ? "Свет" : "Тьма"), Inspect, P.Slot);
            Text(Row, "Active", "Сейчас: " + HeroName(P.Hero, Catalog));
            if (!P.Account)
            {
                Text(Row, "NoId", "Клиент не предоставил публичный ID (бот или скрытый идентификатор). История не запрашивается.", "#a4b3b8", "12px"); Row.Release(); continue;
            }
            const auto Data = PlayerAnalysis::Get(P.Account);
            const auto Summary = MatchPlayerSummary::Build(Data, P.Hero);
            if (Data.Busy) Text(Row, "Loading", "Публичная история загружается…", "#b7c2c6", "12px");
            if (Summary.Matches.empty())
            {
                Text(Row, "NoHistory", Data.Status.empty() ? "Нет доступной публичной истории." : Data.Status, "#a4b3b8", "12px"); Row.Release(); continue;
            }
            auto Sequence = Panel::Create("Panel", Row, "Sequence"); Sequence.SetStyle({{"width", "100%"}, {"flow-children", "right"}, {"margin-bottom", "5px"}});
            unsigned I = 0;
            for (const auto& M : Summary.Matches)
            {
                auto Mark = Panel::Create("Label", Sequence, ("Result" + std::to_string(I++)).c_str());
                Mark.SetBool("hittest", false); Mark.SetStyle({{"width", "21px"}, {"height", "21px"}, {"text-align", "center"}, {"font-size", "12px"}, {"margin-right", "2px"}, {"background-color", M.Won ? "#41644d" : "#693f41"}, {"color", "#eff4ed"}}); Mark.SetText(M.Won ? "W" : "L"); Mark.Release();
            }
            Sequence.Release();
            Text(Row, "Results", "N=" + std::to_string(Summary.Matches.size()) + " · W/L " + std::to_string(Summary.Wins) + "/" + std::to_string(Summary.Matches.size() - Summary.Wins) + " · WR " + Percent(Summary.WinRate()));
            if (P.Hero) Text(Row, "ActiveStats", "Текущий герой: " + std::to_string(Summary.Active.Games) + " игр · WR " + Percent(Summary.Active.WinRate()) + " · PR " + Percent(Summary.PickRate(Summary.Active)), "#c5d6dc", "12px");
            Text(Row, "Favorite", "Частый герой: " + HeroName(Summary.Favorite.Id, Catalog) + " · WR " + Percent(Summary.Favorite.WinRate()) + " · PR " + Percent(Summary.PickRate(Summary.Favorite)), "#d7c08b", "12px");
            for (unsigned H = 1; H < std::min<std::size_t>(3, Summary.Popular.size()); ++H)
            {
                const auto& Usage = Summary.Popular[H];
                Text(Row, ("Popular" + std::to_string(H)).c_str(), HeroName(Usage.Id, Catalog) + " · " + std::to_string(Usage.Games)
                    + " игр · WR " + Percent(Usage.WinRate()) + " · PR " + Percent(Summary.PickRate(Usage)), "#c5d0d4", "12px");
            }
            Text(Row, "Lane", std::string("Чаще линия: ") + Lanes[Summary.BestLane] + " (" + std::to_string(Summary.Lanes[Summary.BestLane]) + "/" + std::to_string(Summary.LaneSample) + " известных)", "#a4b3b8", "12px");
            if (Data.Cached) Text(Row, "Cache", "Ранее полученный публичный кэш.", "#cbb37e", "11px");
            if (Detail == P.Slot)
            {
                I = 0;
                for (const auto& M : Summary.Matches)
                    Text(Row, ("Match" + std::to_string(I++)).c_str(), "#" + std::to_string(M.Id) + " · " + HeroName(M.Hero, Catalog) + (M.Won ? " · W · " : " · L · ") + Value(M.Kills) + "/" + Value(M.Deaths) + "/" + Value(M.Assists), M.Won ? "#a1c6a4" : "#d7aaa5", "11px");
            }
            Row.Release();
        }
        if (Players.empty()) Text(Parent, "Empty", "Ждём список участников от клиента.");
    }
    void PickPanel(const Panel& Root, bool Picking)
    {
        auto ToggleButton = Root.FindChild("OvrPickPlayersToggle");
        if (Picking && !ToggleButton.IsValid())
        {
            Button(Root, "OvrPickPlayersToggle", "Статистика игроков", TogglePick);
            ToggleButton.Release(); ToggleButton = Root.FindChild("OvrPickPlayersToggle");
            ToggleButton.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "left"}, {"vertical-align", "top"}, {"margin-left", "20px"}, {"margin-top", "100px"}, {"z-index", "1000"}});
        }
        ToggleButton.SetVisible(Picking);
        auto Caption = ToggleButton.FindChild("Caption"); Caption.SetText(PickOpen ? "Свернуть статистику игроков" : "Статистика игроков"); Caption.Release(); ToggleButton.Release();
        auto Content = Root.FindChild("OvrPickPlayers"); const auto Current = Revision();
        if (Picking && PickOpen && (!Content.IsValid() || Dirty || Current != PickPainted))
        {
            if (Content.IsValid()) Content.Delete(); Content.Release();
            Content = Panel::Create("Panel", Root, "OvrPickPlayers");
            Content.SetBool("hittest", true); Content.SetBool("hittestchildren", true);
            Content.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "left"}, {"vertical-align", "top"}, {"margin-left", "20px"}, {"margin-top", "138px"},
                {"width", "408px"}, {"height", "66%"}, {"flow-children", "down"}, {"overflow", "squish scroll"}, {"padding", "10px"}, {"background-color", "#182128fa"}, {"border", "1px solid #586a73"}, {"z-index", "1000"}});
            DrawPlayers(Content); PickPainted = Current;
            Log("Pick player statistics drawn: players=%zu", Players.size());
        }
        Content.SetVisible(Picking && PickOpen); Content.Release();
    }
    void DrawResults(const Panel& Parent)
    {
        const auto Catalog = MetaData::Get();
        Text(Parent, "Title", "Итоги текущего матча", "#d7c08b", "22px");
        Text(Parent, "Source", "Локальные данные Players из завершённого матча. Это резервная таблица, а не ответ GC и не история OpenDota.", "#a4b3b8", "12px");
        Button(Parent, "Close", "Закрыть", Results);
        for (const auto& P : Players)
        {
            auto Row = Panel::Create("Panel", Parent, ("Result" + std::to_string(P.Slot)).c_str()); Row.SetStyle({{"width", "100%"}, {"flow-children", "right"}, {"padding", "6px"}, {"margin-bottom", "3px"}, {"background-color", P.Team == 2 ? "#20342c" : "#36292b"}});
            Text(Row, "ResultText", P.Name + " · " + HeroName(P.Hero, Catalog) + " · K/D/A " + Value(P.Kills) + "/" + Value(P.Deaths) + "/" + Value(P.Assists)); Row.Release();
        }
        Text(Parent, "Limits", "Неизвестные значения отмечены прочерком. Штатный послематчевый экран Dota не скрывается и не переключается этой таблицей.", "#a4b3b8", "11px");
    }
    void NativeDetails(std::intptr_t)
    {
        auto Root = Panel::Root(); auto Progress = Root.FindChildTraverse("PostGameProgress");
        auto Loaded = Progress.Parent(), Page = Loaded.Parent();
        if (Page.IsValid() && Page.Type() == "DOTAPostGame")
        {
            V8::Local Args[] = {Panorama::String("DOTAPostGameProgressAnimationComplete"), Page.Handle()};
            Panorama::Invoke(Panorama::Dollar(), "DispatchEvent", 2, Args);
            Log("Postgame: requested native details via original progress-complete event");
        }
        Page.Release(); Loaded.Release(); Progress.Release(); Root.Release();
    }
    void Dashboard()
    {
        static std::uintptr_t LastPage = 0;
        static unsigned LastState = ~0u;
        static std::uintptr_t LastMap = 0;
        static unsigned LastMVPState = ~0u;
        static ULONGLONG MVPWaitingSince = 0;
        static bool ReportedWait = false;
        Panorama::Scope Scope; if (!Scope.IsValid()) return;
        auto Root = Panel::Root(); if (!Root.IsValid()) { Root.Release(); return; }
        auto Progress = Root.FindChildTraverse("PostGameProgress"); auto Loaded = Progress.Parent(), Page = Loaded.Parent();
        if (Page.IsValid() && Page.Type() == "DOTAPostGame")
        {
            const auto PageKey = reinterpret_cast<std::uintptr_t>(Page.Native());
            const unsigned State = (Page.HasClass("MatchDataLoaded") ? 1u : 0u) | (Page.HasClass("MatchDataLoading") ? 2u : 0u) | (Page.HasClass("MatchDataNotFound") ? 4u : 0u) | (Page.HasClass("ProgressVisible") ? 8u : 0u);
            if (PageKey != LastPage || State != LastState)
            {
                Log("Native postgame page: dataLoaded=%u dataLoading=%u dataNotFound=%u progressVisible=%u", State & 1u, (State >> 1) & 1u, (State >> 2) & 1u, (State >> 3) & 1u);
                LastPage = PageKey; LastState = State;
            }
            // Native post_game_mvp.ts first waits for SceneLoaded, spawns the
            // actual match hero, then waits for EntitesLoaded before fading in.
            // Observe those exact states; never manufacture completion classes,
            // a winning player, or progress data to hide a stuck scene.
            auto MVPScreen = Progress.FindChildTraverse("MVP2Screen");
            auto MVPMap = MVPScreen.FindChildTraverse("MVPMap");
            auto MVPDetails = MVPScreen.FindChildTraverse("MVPDetails");
            const auto MapKey = reinterpret_cast<std::uintptr_t>(MVPMap.Native());
            const unsigned MVPState = (MVPScreen.IsValid() ? 1u : 0u) | (MVPMap.HasClass("SceneLoaded") ? 2u : 0u)
                | (MVPMap.HasClass("EntitesLoaded") ? 4u : 0u) | (MVPMap.HasClass("MapFadeIn") ? 8u : 0u)
                | (MVPDetails.HasClass("MVPDetailsAnimateIn") ? 16u : 0u);
            if (MapKey != LastMap || MVPState != LastMVPState)
            {
                Log("Native MVP progress: screen=%u sceneLoaded=%u entitiesLoaded=%u mapFadeIn=%u detailsVisible=%u gameState=%u", MVPState & 1u,
                    (MVPState >> 1) & 1u, (MVPState >> 2) & 1u, (MVPState >> 3) & 1u, (MVPState >> 4) & 1u, static_cast<unsigned>(C_DOTAGamerules::CurrentState()));
                LastMap = MapKey; LastMVPState = MVPState; MVPWaitingSince = GetTickCount64(); ReportedWait = false;
            }
            const bool Waiting = (State & 1u) && (State & 8u) && MapKey && (MVPState & 6u) != 6u;
            if (Waiting && MVPWaitingSince && GetTickCount64() - MVPWaitingSince >= 20000 && !ReportedWait)
            {
                ReportedWait = true;
                LogW("Native MVP scene still waiting after 20s: sceneLoaded=%u entitiesLoaded=%u; native match data is present", (MVPState >> 1) & 1u, (MVPState >> 2) & 1u);
            }
            MVPDetails.Release(); MVPMap.Release(); MVPScreen.Release();
            auto ButtonPanel = Page.FindChild("OvrNativePostgameDetails");
            if (!ButtonPanel.IsValid())
            {
                Button(Page, "OvrNativePostgameDetails", "Показать таблицу матча", NativeDetails);
                ButtonPanel.Release(); ButtonPanel = Page.FindChild("OvrNativePostgameDetails");
                ButtonPanel.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "right"}, {"vertical-align", "top"}, {"width", "238px"}, {"margin-top", "128px"}, {"margin-right", "32px"}, {"z-index", "50"}});
            }
            ButtonPanel.SetVisible(Page.HasClass("MatchDataLoaded") && Page.HasClass("ProgressVisible")); ButtonPanel.Release();
        }
        Page.Release(); Loaded.Release(); Progress.Release(); Root.Release();
    }
}

void MatchPlayersPanel::Update()
{
    if (!LocalPlus::Enabled() || GetTickCount64() < Next) return;
    Next = GetTickCount64() + 1500;
    const auto State = C_DOTAGamerules::CurrentState();
    if (State != PreviousState)
    {
        if (ActiveMatch(State) && State != EGameState::PostGame && (PreviousState == EGameState::Unknown || PreviousState == EGameState::Disconnect || PreviousState == EGameState::PostGame))
        {
            Players.clear(); LoggedRoster = 0; Detail = -1; ResultsOpen = false; Dirty = true;
        }
        if (State == EGameState::PostGame)
        {
            ResultsOpen = false;
            Log("Postgame entered: preserving native page; local scoreboard fallback available");
        }
        if (MatchPanelPlan::Picking(State) && !MatchPanelPlan::Picking(PreviousState)) { PickOpen = true; Dirty = true; }
        PreviousState = State;
    }
    bool HasHud = false;
    {
        Panorama::Scope Scope(Panorama::RootKind::Hud);
        if (Scope.IsValid())
        {
            auto Root = Panel::Root();
            if (Root.IsValid())
            {
                HasHud = true;
                if (ActiveMatch(State)) { PollPlayers(); MetaData::Request(); }
                PickPanel(Root, MatchPanelPlan::Picking(State));
                auto& Board = FindScoreboard(Root);
                if (Board.IsValid())
                {
                    auto ToggleButton = Board.FindChild("OvrMatchPlayersToggle");
                    if (!ToggleButton.IsValid())
                    {
                        Button(Board, "OvrMatchPlayersToggle", "Последние 15 матчей", Toggle);
                        ToggleButton.Release(); ToggleButton = Board.FindChild("OvrMatchPlayersToggle");
                        ToggleButton.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "right"}, {"vertical-align", "top"}, {"margin-top", "6px"}, {"margin-right", "8px"}, {"z-index", "10"}});
                    }
                    ToggleButton.SetVisible(State != EGameState::PostGame); ToggleButton.Release();
                    auto Content = Board.FindChild("OvrMatchPlayers"); const auto RevisionNow = Revision();
                    if (Open && State != EGameState::PostGame && (!Content.IsValid() || Dirty || RevisionNow != Painted))
                    {
                        if (Content.IsValid()) Content.Delete(); Content.Release();
                        Content = Panel::Create("Panel", Board, "OvrMatchPlayers");
                        Content.SetBool("hittest", true); Content.SetBool("hittestchildren", true);
                        Content.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "right"}, {"vertical-align", "top"}, {"margin-top", "42px"}, {"margin-right", "8px"}, {"width", "436px"}, {"height", "736px"}, {"flow-children", "down"}, {"overflow", "squish scroll"}, {"padding", "10px"}, {"background-color", "#182128fa"}, {"border", "1px solid #586a73"}, {"z-index", "9"}});
                        DrawPlayers(Content); Painted = RevisionNow;
                    }
                    Content.SetVisible(Open && State != EGameState::PostGame); Content.Release();
                }
                auto ResultsButton = Root.FindChild("OvrMatchResultsToggle");
                if (State == EGameState::PostGame && !ResultsButton.IsValid())
                {
                    Button(Root, "OvrMatchResultsToggle", "Итоги: локальная K/D/A", Results);
                    ResultsButton.Release(); ResultsButton = Root.FindChild("OvrMatchResultsToggle");
                    ResultsButton.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "right"}, {"vertical-align", "bottom"}, {"margin-bottom", "180px"}, {"margin-right", "24px"}, {"z-index", "20"}});
                }
                ResultsButton.SetVisible(State == EGameState::PostGame && !Players.empty()); ResultsButton.Release();
                auto ResultsPanel = Root.FindChild("OvrMatchLocalResults"); const auto RevisionNow = Revision();
                if (ResultsOpen && State == EGameState::PostGame && (!ResultsPanel.IsValid() || Dirty || RevisionNow != ResultsPainted))
                {
                    if (ResultsPanel.IsValid()) ResultsPanel.Delete(); ResultsPanel.Release();
                    ResultsPanel = Panel::Create("Panel", Root, "OvrMatchLocalResults"); ResultsPanel.SetBool("hittest", true); ResultsPanel.SetBool("hittestchildren", true);
                    ResultsPanel.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "center"}, {"vertical-align", "center"}, {"width", "760px"}, {"max-height", "600px"}, {"flow-children", "down"}, {"padding", "18px"}, {"overflow", "squish scroll"}, {"background-color", "#182128fa"}, {"border", "1px solid #6b6b51"}, {"z-index", "500"}});
                    DrawResults(ResultsPanel); ResultsPainted = RevisionNow;
                }
                ResultsPanel.SetVisible(ResultsOpen && State == EGameState::PostGame && !Players.empty()); ResultsPanel.Release();
            }
            Root.Release();
        }
    }
    {
        Panorama::Scope Scope;
        if (Scope.IsValid())
        {
            auto Root = Panel::Root();
            if (Root.IsValid())
            {
                if (!HasHud && MatchPanelPlan::Picking(State)) { PollPlayers(); MetaData::Request(); }
                PickPanel(Root, !HasHud && MatchPanelPlan::Picking(State));
            }
            Root.Release();
        }
    }
    Dashboard(); Dirty = false;
}
