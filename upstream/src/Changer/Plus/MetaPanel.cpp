#include "MetaPanel.h"
#include "MetaData.h"
#include "BuildData.h"
#include "ProTrackerData.h"
#include "PlayerAnalysis.h"
#include "VisionTracker.h"
#include "NativeIntegration.h"
#include "MinimapOverlay.h"
#include "LocalPlus.h"
#include "SDK/Panorama/Panel.h"
#include "SDK/GameRules/C_DOTAGamerules.h"
#include "SDK/SteamAPI/ISteamUser.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace
{
    bool Open = false, Hud = false, Dirty = true, Ascending = false;
    unsigned Tab = 0, Rank = 0, Lane = 0, Sort = 0, Page = 0, Selected = 1, Minimum = 1000, Stage = 1;
    ULONGLONG Next = 0;
    std::uint64_t LastRevision = 0, LastDetail = 0, LastPlayer = 0;
    double LastVisionTime = -1;
    std::string LastMapStatus;
    bool LastBuildBusy = false;
    unsigned LastBuildHero = 0;
    unsigned RequestedAnalysis = 0;
    unsigned HistoryPage = 0;
    std::uint64_t PlayerViewVersion = 0;
    std::string AnalysisInput;
    std::string AnalysisError;
    std::string AnalysisNotice;
    const char* Ranks[] = {"Все ранги", "Herald", "Guardian", "Crusader", "Archon", "Legend", "Ancient", "Divine", "Immortal"};
    const char* Lanes[] = {"Все линии", "Безопасная", "Центр", "Сложная", "Лес / роум"};
    const char* SortNames[] = {"Винрейт", "Пикрейт", "Число игр", "Имя"};
    V8::Local Object(const char* Name) { return V8::Get(Panorama::Isolate(), Panorama::Global(), Panorama::Context(), Name); }
    double Call(const char* Owner, const char* Method, int Argc = 0, V8::Local* Args = nullptr)
    {
        auto V = Panorama::Invoke(Object(Owner), Method, Argc, Args);
        return V && V8::TypeOf(Panorama::Isolate(), V) == "number" ? V8::NumberValue(V, Panorama::Context()) : -1;
    }
    std::string Format(double N, const char* Suffix = "%") { if (N < 0 || !std::isfinite(N)) return "—"; char B[48]; std::snprintf(B, sizeof(B), "%.1f%s", N, Suffix); return B; }
    void Text(const Panel& P, const char* Id, const std::string& Value, const char* Size = "14px", const char* Color = "#e4e9e5", const char* Width = "100%")
    {
        auto L = Panel::Create("Label", P, Id); L.SetBool("hittest", false);
        L.SetStyle({{"font-family", "Radiance"}, {"font-size", Size}, {"color", Color}, {"width", Width}, {"white-space", "normal"}, {"margin-bottom", "5px"}});
        L.SetText(Value.c_str()); L.Release();
    }
    void Button(const Panel& P, const char* Id, const std::string& Value, Panorama::Handler Handler, std::intptr_t Argument = 0)
    {
        auto B = Panel::Create("Button", P, Id); B.SetBool("hittest", true); B.SetBool("hittestchildren", false);
        B.SetStyle({{"width", "fit-children"}, {"min-width", "75px"}, {"height", "34px"}, {"padding", "6px 10px"}, {"background-color", "#2d3c45"}, {"margin-right", "6px"}, {"margin-bottom", "5px"}});
        Text(B, "Caption", Value, "14px", "#e4e9e5", "fit-children"); B.SetEvent("onactivate", Handler, Argument); B.Release();
    }
    Panel Row(const Panel& P, const char* Id) { auto R = Panel::Create("Panel", P, Id); R.SetStyle({{"width", "100%"}, {"flow-children", "right"}, {"min-height", "38px"}}); return R; }
    void Close(std::intptr_t) { Open = false; Dirty = true; }
    void SetTab(std::intptr_t N) { Tab = static_cast<unsigned>(N); if (Tab == 1) { Sort = 0; Ascending = false; } Dirty = true; }
    void CycleRank(std::intptr_t) { Rank = (Rank + 1) % 9; Lane = 0; Page = 0; Dirty = true; }
    void CycleLane(std::intptr_t) { Lane = (Lane + 1) % 5; if (Lane) { Rank = 0; Minimum = 10; } Page = 0; Dirty = true; }
    void CycleSort(std::intptr_t) { Sort = (Sort + 1) % 4; Ascending = Sort == 3; Page = 0; Dirty = true; }
    void Direction(std::intptr_t) { Ascending = !Ascending; Page = 0; Dirty = true; }
    void Samples(std::intptr_t) { Minimum = Minimum == 0 ? 10 : Minimum == 10 ? 100 : Minimum == 100 ? 1000 : 0; Page = 0; Dirty = true; }
    void Nav(std::intptr_t N) { if (N < 0) { if (Page) --Page; } else ++Page; Dirty = true; }
    void Hero(std::intptr_t Id) { Selected = static_cast<unsigned>(Id); MetaData::RequestDetail(Selected); BuildData::Request(Selected); Dirty = true; }
    void BuildStage(std::intptr_t N) { Stage = static_cast<unsigned>(N); Dirty = true; }
    void ToggleShop(std::intptr_t) { NativeIntegration::ToggleShop(0); Dirty = true; }
    void ToggleRecommended(std::intptr_t) { ProTrackerData::Toggle(); Dirty = true; }
    void Refresh(std::intptr_t) { MetaData::Request(); Dirty = true; }
    void AnalyzeMe(std::intptr_t) { if (auto* U = ISteamUser::Get()) { RequestedAnalysis = U->AccountId(); HistoryPage = 0; PlayerAnalysis::Request(RequestedAnalysis); AnalysisError.clear(); } Dirty = true; ++PlayerViewVersion; }
    void Analyze(std::intptr_t Id) { if (Id > 0 && Id <= UINT32_MAX) { RequestedAnalysis = static_cast<unsigned>(Id); HistoryPage = 0; PlayerAnalysis::Request(RequestedAnalysis); AnalysisError.clear(); } Dirty = true; ++PlayerViewVersion; }
    void HistoryNav(std::intptr_t Direction) { if (Direction < 0) { if (HistoryPage) --HistoryPage; } else ++HistoryPage; Dirty = true; ++PlayerViewVersion; }
    void PrepareBans(std::intptr_t Account) { NativeIntegration::AnalyzeBans(Account); AnalysisNotice = "Подписи банов подготовлены. Открой обычную вкладку «Герои». Настройки — кнопка «Статистика» внизу сетки."; Dirty = true; ++PlayerViewVersion; }
    void AnalyzeEntry(std::intptr_t Embedded)
    {
        auto Root = Panel::Root(), Entry = Root.FindChildTraverse(Embedded ? "OvrEmbeddedAccountInput" : "OvrAccountInput");
        auto V = Entry.Handle() ? V8::Get(Panorama::Isolate(), Entry.Handle(), Panorama::Context(), "text") : nullptr;
        if (V)
        {
            auto S = V8::ToString(Panorama::Isolate(), V); unsigned Id = 0;
            auto Parsed = std::from_chars(S.data(), S.data() + S.size(), Id);
            if (Id && Parsed.ec == std::errc() && Parsed.ptr == S.data() + S.size()) Analyze(Id);
            else { AnalysisError = "Введи числовой Steam account ID (32 бита)."; Dirty = true; ++PlayerViewVersion; }
        }
        Entry.Release(); Root.Release();
    }
    const std::vector<AssistantData::Item>& Items(const AssistantData::Snapshot& S, unsigned Phase)
    { return Phase == 0 ? S.StartingItems : Phase == 1 ? S.EarlyItems : Phase == 2 ? S.MidItems : S.LateItems; }
    void ShowItems(const Panel& P, const AssistantData::Snapshot& Build, unsigned Phase, unsigned Maximum = 8)
    {
        Text(P, "ItemSource", Build.ItemsStatus, "12px", "#91a3ab");
        unsigned I = 0;
        for (const auto& Item : Items(Build, Phase))
        {
            if (I++ >= Maximum) break;
            auto R = Row(P, ("Item" + std::to_string(I)).c_str());
            auto Icon = Panel::Create("DOTAItemImage", R, "Icon"); Icon.SetStyle({{"width", "48px"}, {"height", "34px"}, {"margin-right", "8px"}});
            if (auto H = Icon.Handle()) V8::Set(H, Panorama::Context(), Panorama::String("itemname"), Panorama::String(Item.Name.c_str())); Icon.Release();
            V8::Local Args[] = {Panorama::String(("#DOTA_Tooltip_ability_" + Item.Name).c_str())}; auto Name = Panorama::Invoke(Panorama::Dollar(), "Localize", 1, Args);
            Text(R, "Name", (Name ? V8::ToString(Panorama::Isolate(), Name) : Item.Name) + " · покупок " + std::to_string(static_cast<unsigned>(Item.Frequency)), "14px", "#e4e9e5", "fill-parent-flow(1)"); R.Release();
        }
    }
    void AnalyzePlayers(const Panel& P, bool InHud, bool Embedded = false)
    {
        Text(P, "Title", "Статистика игроков", "22px", "#c4aa73");
        auto Controls = Row(P, "Accounts");
        Button(Controls, "Me", "Моя статистика", AnalyzeMe);
        auto Entry = Panel::Create("TextEntry", Controls, Embedded ? "OvrEmbeddedAccountInput" : "OvrAccountInput"); Entry.SetStyle({{"width", "180px"}, {"height", "32px"}, {"background-color", "#30414c"}, {"color", "#ffffff"}, {"margin-right", "6px"}}); Entry.SetBool("hittest", true);
        if (auto E = Entry.Handle()) V8::Set(E, Panorama::Context(), Panorama::String("text"), Panorama::String(AnalysisInput.c_str())); Entry.Release();
        Button(Controls, "Analyze", "По account ID", AnalyzeEntry, Embedded ? 1 : 0); Controls.Release();
        Text(P, "InputHint", "Steam account ID — число, не SteamID64. Анализ только доступных публичных матчей.", "12px", "#91a3ab");
        if (!AnalysisError.empty()) Text(P, "InputError", AnalysisError, "13px", "#e18b83");
        if (InHud)
        {
            auto Players = Row(P, "Players"); auto Ids = Panorama::Invoke(Object("Game"), "GetAllPlayerIDs");
            if (Ids && V8::IsObject(Ids)) for (unsigned I = 0; I < std::min(10u, V8::ArrayLength(Ids)); ++I)
            {
                auto Id = V8::GetIndex(Ids, Panorama::Context(), I); V8::Local Args[] = {Id};
                auto Info = Panorama::Invoke(Object("Game"), "GetPlayerInfo", 1, Args);
                if (!Info || !V8::IsObject(Info)) continue;
                auto Steam = V8::Get(Panorama::Isolate(), Info, Panorama::Context(), "player_steamid");
                auto Name = V8::Get(Panorama::Isolate(), Info, Panorama::Context(), "player_name");
                if (!Steam || !Name || V8::TypeOf(Panorama::Isolate(), Steam) != "string") continue;
                auto S = V8::ToString(Panorama::Isolate(), Steam); std::uint64_t Value = 0; auto Parsed = std::from_chars(S.data(), S.data() + S.size(), Value);
                if (Parsed.ec == std::errc() && Parsed.ptr == S.data() + S.size() && Value > 76561197960265728ull && Value <= 76561197960265728ull + UINT32_MAX)
                    Button(Players, ("Player" + std::to_string(I)).c_str(), V8::ToString(Panorama::Isolate(), Name), Analyze, static_cast<unsigned>(Value - 76561197960265728ull));
            }
            Players.SetStyle("flow-children", "right-wrap"); Players.Release();
        }
        const auto Data = PlayerAnalysis::Get();
        if (Data.Account) Text(P, "Account", "Анализ account ID: " + std::to_string(Data.Account), "13px", "#91a3ab");
        Text(P, "PlayerStatus", Data.Status, "15px"); Text(P, "Role", Data.Role, "15px", "#c4aa73");
        if (Data.FetchedAt) Text(P, "CacheAge", "Данные загружены " + std::to_string(std::max<std::int64_t>(0, static_cast<std::int64_t>(std::time(nullptr)) - static_cast<std::int64_t>(Data.FetchedAt)) / 60) + " мин назад" + (Data.Cached ? " · ранее полученный публичный кэш" : ""), "12px", "#91a3ab");
        auto Scroll = Panel::Create("Panel", P, "PlayerAnalysisScroll"); Scroll.SetStyle({{"width", "100%"}, {"height", "fill-parent-flow(1)"}, {"flow-children", "down"}, {"overflow", "squish scroll"}});
        const auto Catalog = MetaData::Get();
        auto HeroName = [&](unsigned Id) { auto It = std::find_if(Catalog.Heroes.begin(), Catalog.Heroes.end(), [&](const auto& H) { return H.Id == Id; }); return It == Catalog.Heroes.end() ? "hero " + std::to_string(Id) : It->Name; };
        auto Date = [](std::uint32_t Value) { if (!Value) return std::string("дата неизвестна"); const std::time_t T = Value; std::tm Local{}; char B[24]{}; if (localtime_s(&Local, &T)) return std::string("дата неизвестна"); std::strftime(B, sizeof(B), "%d.%m.%Y", &Local); return std::string(B); };
        Text(Scroll, "PicksTitle", "Частые пики", "20px", "#c4aa73"); Text(Scroll, "PicksSource", Data.HeroSource, "12px", "#91a3ab");
        for (unsigned I = 0; I < std::min<std::size_t>(8, Data.Frequent.size()); ++I)
        {
            const auto& H = Data.Frequent[I];
            Text(Scroll, ("Frequent" + std::to_string(I)).c_str(), HeroName(H.Hero) + " · игр " + std::to_string(H.Games) + " · WR " + Format(H.Games ? H.Wins * 100. / H.Games : -1) + " · последний публичный матч " + Date(H.LastPlayed));
        }
        if (!Data.RecentHeroes.empty())
        {
            Text(Scroll, "RecentPicksTitle", "Пики в последних 30 доступных матчах", "17px", "#c4aa73");
            for (unsigned I = 0; I < std::min<std::size_t>(5, Data.RecentHeroes.size()); ++I)
            {
                const auto& H = Data.RecentHeroes[I]; Text(Scroll, ("RecentHero" + std::to_string(I)).c_str(), HeroName(H.Hero) + " · " + std::to_string(H.Games) + " игр · WR " + Format(H.Wins * 100. / H.Games));
            }
        }
        if (!Data.Frequent.empty()) Button(Scroll, "BanThisPlayer", "Рекомендации банов против этого игрока в сетке героев", PrepareBans, Data.Account);
        if (!AnalysisNotice.empty()) Text(Scroll, "BanNotice", AnalysisNotice, "13px", "#c4aa73");
        Text(Scroll, "SmurfTitle", "Признаки смурфа: эвристика", "20px", "#c4aa73");
        const auto& Signals = Data.Patterns;
        Text(Scroll, "SmurfAssessment", Signals.Assessment.empty() ? "Нет доступной рейтинговой выборки для оценки." : Signals.Assessment);
        Text(Scroll, "SmurfNumbers", "Рейтинговых матчей: " + std::to_string(Signals.RankedSample) + " · WR " + Format(Signals.RankedSample ? Signals.RankedWins * 100. / Signals.RankedSample : -1) + " · медиана KDA " + Format(Signals.MedianKda, "") + " (N=" + std::to_string(Signals.KdaSample) + ")", "13px");
        Text(Scroll, "SmurfLimits", "Проверяются WR≥75% при нижней границе Wilson≥60% и медиана KDA≥5, от 20 рейтинговых матчей. Роли, сила соперников и возраст аккаунта не нормализованы. Это не вероятность и не подтверждение смурфа.", "12px", "#91a3ab");
        if (!Data.Matches.empty())
        {
            Text(Scroll, "Streak", "Серия в доступной выборке: " + std::to_string(Signals.CurrentStreak) + (Signals.StreakWon ? " побед" : " поражений") + " · максимум W/L: " + std::to_string(Signals.LongestWins) + "/" + std::to_string(Signals.LongestLosses), "17px", "#c4aa73");
            Text(Scroll, "StreakLimit", "В публичной истории могут быть пропуски. Серия по доступным матчам может отличаться от реальной непрерывной серии.", "12px", "#91a3ab");
            Text(Scroll, "Summary", "Матчей: " + std::to_string(Data.Matches.size()) + " · побед: " + std::to_string(Data.Wins) + " · WR " + Format(Data.Wins * 100. / Data.Matches.size()), "20px");
            HistoryPage = std::min(HistoryPage, static_cast<unsigned>((Data.Matches.size() - 1) / 10));
            auto Pages = Row(Scroll, "HistoryPages"); Button(Pages, "HistoryBack", "Назад", HistoryNav, -1); Text(Pages, "HistoryPage", "Страница " + std::to_string(HistoryPage + 1), "14px", "#91a3ab", "130px"); Button(Pages, "HistoryNext", "Далее", HistoryNav, 1); Pages.Release();
            for (unsigned I = HistoryPage * 10; I < Data.Matches.size() && I < (HistoryPage + 1) * 10; ++I)
            {
                const auto& M = Data.Matches[I]; auto Stat = [](int N) { return N < 0 ? std::string("—") : std::to_string(N); };
                auto Hero = std::find_if(Catalog.Heroes.begin(), Catalog.Heroes.end(), [&](const auto& H) { return H.Id == M.Hero; });
                Text(Scroll, ("Match" + std::to_string(I)).c_str(), "#" + std::to_string(M.Id) + " · " + (Hero == Catalog.Heroes.end() ? "hero " + std::to_string(M.Hero) : Hero->Name) + (M.Won ? " · победа · " : " · поражение · ") + Stat(M.Kills) + "/" + Stat(M.Deaths) + "/" + Stat(M.Assists));
            }
        }
        Scroll.Release();
    }
    void Observations(const Panel& P)
    {
        const auto Data = VisionTracker::Get();
        Text(P, "Title", "Карта и лес", "24px", "#c4aa73");
        Text(P, "Status", Data.Status.empty() ? "Наблюдения появятся в матче. Данные из тумана войны не используются." : Data.Status, "13px", "#91a3ab");
        Text(P, "MinimapStatus", MinimapOverlay::Status(), "13px", "#c4aa73");
        auto MapControls = Row(P, "MinimapControls");
        Button(MapControls, "MapToggle", MinimapOverlay::Enabled() ? "Выключить метки" : "Включить метки", MinimapOverlay::Toggle);
        Button(MapControls, "MapCheck", "Проверить положение отметок", MinimapOverlay::CheckAlignment); MapControls.Release();
        Text(P, "MapCheckHint", "Контрольный жёлтый контур должен совпасть с твоим героем на штатной мини-карте.", "12px", "#91a3ab");
        auto Scroll = Panel::Create("Panel", P, "ObservedObjects"); Scroll.SetStyle({{"width", "100%"}, {"height", "fill-parent-flow(1)"}, {"flow-children", "down"}, {"overflow", "squish scroll"}});
        Text(Scroll, "WardsTitle", "Вражеские варды и курьеры: последние наблюдения", "19px", "#c4aa73");
        unsigned I = 0;
        for (const auto& O : Data.EnemyObjects)
        {
            if (O.Trail.empty()) continue; const auto& Last = O.Trail.back();
            Text(Scroll, ("Object" + std::to_string(I)).c_str(), O.Kind + " · X " + Format(Last.X, "") + " / Y " + Format(Last.Y, "") + " · видели " + std::to_string(static_cast<unsigned>(std::max(0., Data.Time - Last.Time))) + "с назад", "15px");
            if (O.Courier)
            {
                std::string Trail = "Наблюдавшийся путь: ";
                for (const auto& Point : O.Trail) Trail += "(" + std::to_string(static_cast<int>(Point.X)) + "," + std::to_string(static_cast<int>(Point.Y)) + ") ";
                Text(Scroll, ("Trail" + std::to_string(I)).c_str(), Trail, "12px", "#91a3ab");
            }
            ++I;
        }
        if (!I) Text(Scroll, "Empty", "Подтверждённых наблюдений нет.");
        Text(Scroll, "JungleTitle", "Маршрут через ближайшие видимые лагеря", "19px", "#c4aa73");
        I = 0;
        for (const auto& Point : Data.JungleRoute)
        {
            ++I;
            Text(Scroll, ("Camp" + std::to_string(I)).c_str(), std::to_string(I) + ". X " + Format(Point.X, "") + " / Y " + Format(Point.Y, ""));
        }
        if (!I) Text(Scroll, "NoCamps", "Сейчас нет подтверждённых видимых лагерей. Открой обзор леса.");
        Text(Scroll, "RouteLimit", "Ближайшие точки по расстоянию, без проверки силы лагеря и безопасности пути. Герой сам не перемещается.", "12px", "#91a3ab");
        Scroll.Release();
    }
    void Render(const Panel& Window, const MetaData::Snapshot& Data)
    {
        auto OldInput = Window.FindChildTraverse("OvrAccountInput");
        if (auto E = OldInput.Handle()) { auto V = V8::Get(Panorama::Isolate(), E, Panorama::Context(), "text"); if (V) AnalysisInput = V8::ToString(Panorama::Isolate(), V); } OldInput.Release();
        auto Content = Window.FindChild("OvrMetaContent"); if (Content.IsValid()) Content.Delete(); Content.Release();
        Content = Panel::Create("Panel", Window, "OvrMetaContent"); Content.SetStyle({{"width", "100%"}, {"height", "fill-parent-flow(1)"}, {"flow-children", "down"}, {"padding", "14px"}});
        // Meta and builds belong to the native hero grid/shop, not this window.
        if (Tab == 3) { AnalyzePlayers(Content, Hud); Content.Release(); return; }
        if (Tab == 4) { Observations(Content); Content.Release(); return; }
        if (Tab == 5)
        {
            Text(Content, "Title", "Настройки магазина", "22px", "#d8bd82");
            Button(Content, "RecommendedBuilds", ProTrackerData::Enabled() ? "Рекомендованные сборки: включены" : "Рекомендованные сборки: выключены", ToggleRecommended);
            Text(Content, "Source", "DotaCoach: предметы по стадиям, прокачка способностей и таланты для выбранной позиции. Позиция переключается прямо в колонке магазина.", "14px");
            Text(Content, "Restore", "Выключи режим, чтобы вернуться к выбранной обычной сборке.", "13px", "#a4b4bc");
            Content.Release(); return;
        }
        auto Controls = Row(Content, "Controls");
        Button(Controls, "Rank", Ranks[Rank], CycleRank); Button(Controls, "Lane", Lanes[Lane], CycleLane); Button(Controls, "Sort", std::string("Сортировка: ") + SortNames[Sort], CycleSort);
        Button(Controls, "Order", Ascending ? "По возрастанию" : "По убыванию", Direction); Button(Controls, "Minimum", "Мин. игр: " + std::to_string(Minimum), Samples); Button(Controls, "Refresh", "Обновить (15 мин)", Refresh); Controls.Release();
        Text(Content, "Status", Data.Status, "12px", "#91a3ab");
        if (Data.Updated) Text(Content, "Updated", "Загружено: " + std::to_string((std::time(nullptr) - Data.Updated) / 60) + " мин назад. Окно выборки задаёт источник; привязка к текущему патчу не гарантируется.", "11px", "#91a3ab");
        if (Lane) Text(Content, "LaneSource", Data.LaneStatus, "12px", "#c4aa73");
        if (Tab == 3) { AnalyzePlayers(Content, Hud); Content.Release(); return; }
        if (Tab == 4) { Observations(Content); Content.Release(); return; }
        auto Main = Row(Content, "Main"); Main.SetStyle("height", "fill-parent-flow(1)");
        auto Left = Panel::Create("Panel", Main, "GridArea"); Left.SetStyle({{"width", "fill-parent-flow(1)"}, {"height", "100%"}, {"flow-children", "down"}, {"margin-right", "16px"}});
        auto Heroes = MetaData::Sorted(Data, Rank, Lane, Sort, Ascending);
        std::erase_if(Heroes, [&](const auto& H) { return (Lane ? H.Lanes[Lane] : H.Ranks[Rank]).Picks < Minimum; });
        // Exclude already selected draft heroes; only public selected-hero IDs.
        if (Tab == 1 && Hud)
        {
            auto Ids = Panorama::Invoke(Object("Game"), "GetAllPlayerIDs");
            if (Ids && V8::IsObject(Ids)) for (unsigned I = 0; I < std::min(24u, V8::ArrayLength(Ids)); ++I)
            {
                V8::Local A[] = {V8::GetIndex(Ids, Panorama::Context(), I)}; auto HeroId = Call("Players", "GetSelectedHeroID", 1, A);
                std::erase_if(Heroes, [&](const auto& H) { return H.Id == HeroId; });
            }
        }
        if (Tab == 1) Text(Left, "PickMethod", "Рекомендации по винрейту выбранного среза и линии. Уже выбранные герои исключаются. Это статистический рейтинг, не ИИ и не прогноз победы.", "13px", "#c4aa73");
        Page = std::min(Page, Heroes.empty() ? 0u : static_cast<unsigned>((Heroes.size() - 1) / 16));
        auto Grid = Panel::Create("Panel", Left, "Grid"); Grid.SetStyle({{"width", "100%"}, {"height", "fill-parent-flow(1)"}, {"flow-children", "right-wrap"}, {"overflow", "squish scroll"}});
        for (unsigned I = Page * 16; I < Heroes.size() && I < (Page + 1) * 16; ++I)
        {
            const auto& H = Heroes[I]; const auto& Stats = Lane ? H.Lanes[Lane] : H.Ranks[Rank];
            auto Card = Panel::Create("Button", Grid, ("Hero" + std::to_string(H.Id)).c_str()); Card.SetBool("hittest", true); Card.SetBool("hittestchildren", false);
            Card.SetStyle({{"width", "23.5%"}, {"height", "126px"}, {"flow-children", "down"}, {"margin-right", "1%"}, {"margin-bottom", "8px"}, {"padding", "7px"}, {"background-color", H.Id == Selected ? "#3a4a45" : "#243139"}, {"border-bottom", H.Id == Selected ? "2px solid #c4aa73" : "2px solid #34454e"}});
            auto Portrait = Panel::Create("DOTAHeroImage", Card, "Portrait"); Portrait.SetStyle({{"width", "100%"}, {"height", "52px"}});
            if (auto Image = Portrait.Handle()) V8::Set(Image, Panorama::Context(), Panorama::String("heroid"), Panorama::Number(H.Id)); Portrait.Release();
            Text(Card, "Name", H.Name, "16px"); Text(Card, "Rates", "WR " + Format(Stats.WinRate()) + "   PR≈" + Format(MetaData::PickRate(Data.Heroes, H, Rank)), "13px", "#c4aa73");
            Text(Card, "Games", "Игр " + std::to_string(static_cast<std::uint64_t>(Stats.Picks)), "11px", "#91a3ab"); Card.SetEvent("onactivate", Hero, H.Id); Card.Release();
        }
        if (Heroes.empty()) Text(Grid, "Empty", Data.Busy ? "Загрузка…" : "В этом срезе данных нет. Снизь минимум игр или смени фильтр."); Grid.Release();
        auto Navigation = Row(Left, "Pages"); Button(Navigation, "Previous", "Назад", Nav, -1); Text(Navigation, "Page", std::to_string(Page + 1) + " / " + std::to_string(std::max<std::size_t>(1, (Heroes.size() + 15) / 16)), "14px", "#91a3ab", "110px"); Button(Navigation, "Next", "Далее", Nav, 1); Navigation.Release(); Left.Release();
        auto Details = Panel::Create("Panel", Main, "Details"); Details.SetStyle({{"width", "310px"}, {"height", "100%"}, {"flow-children", "down"}, {"overflow", "squish scroll"}});
        auto H = std::find_if(Data.Heroes.begin(), Data.Heroes.end(), [&](const auto& Hero) { return Hero.Id == Selected; });
        Text(Details, "HeroName", H == Data.Heroes.end() ? "Выбери героя" : H->Name, "24px", "#c4aa73");
        const auto Detail = MetaData::GetDetail();
        if (Tab == 2)
        {
            auto Stages = Row(Details, "Stages"); const char* Names[] = {"Старт", "Рано", "Середина", "Поздно"};
            for (unsigned I = 0; I < 4; ++I) Button(Stages, ("Phase" + std::to_string(I)).c_str(), Names[I], BuildStage, I);
            Stages.SetStyle("flow-children", "right-wrap"); Stages.Release();
            ShowItems(Details, BuildData::Get(Selected), Stage);
            Button(Details, "Shop", NativeIntegration::ShopEnabled() ? "Plus Guide: OpenDota" : "Plus Guide: штатные рекомендации", ToggleShop);
            Text(Details, "BuildLimit", "Режим магазина использует твоего текущего героя и стадию игры. Частота покупок не учитывает состав противника.", "12px", "#91a3ab");
        }
        else
        {
            Text(Details, "DetailStatus", Detail.HeroId == Selected ? Detail.Status : "Выбери карточку для загрузки аспектов и матчапов.", "12px", "#91a3ab");
            if (Detail.HeroId == Selected)
            {
                for (const auto& F : Detail.Facets) Text(Details, ("Facet" + std::to_string(F.Variant)).c_str(), "Аспект №" + std::to_string(F.Variant) + " · WR " + Format(F.Stats.WinRate()) + " · игр " + std::to_string(static_cast<unsigned>(F.Stats.Picks)));
                if (!Detail.Busy && Detail.Facets.empty()) Text(Details, "NoFacets", "В выборке нет hero_variant. Статистика аспектов недоступна.", "13px");
                auto Matchups = Detail.Matchups;
                std::erase_if(Matchups, [](const auto& M) { return M.Stats.Picks < 20; });
                std::sort(Matchups.begin(), Matchups.end(), [](const auto& A, const auto& B) { return A.Stats.WinRate() > B.Stats.WinRate(); });
                Text(Details, "MatchupsTitle", "Успешные матчапы (от 20 игр)", "17px", "#c4aa73");
                for (unsigned I = 0; I < std::min<std::size_t>(5, Matchups.size()); ++I)
                {
                    const auto& M = Matchups[I]; auto Other = std::find_if(Data.Heroes.begin(), Data.Heroes.end(), [&](const auto& H) { return H.Id == M.Hero; });
                    Text(Details, ("Vs" + std::to_string(I)).c_str(), (Other == Data.Heroes.end() ? "hero " + std::to_string(M.Hero) : Other->Name) + " · " + Format(M.Stats.WinRate()) + " · N=" + std::to_string(static_cast<unsigned>(M.Stats.Picks)));
                }
            }
        }
        Details.Release(); Main.Release(); Content.Release();
    }

}

void MetaPanel::Toggle(std::intptr_t UseHud)
{
    bool Target = UseHud != 0; Open = !Open || Target != Hud; Hud = Target; Dirty = true; Next = 0; if (Open) Tab = 3;
    if (Open) { MetaData::Request(); BuildData::Request(Selected); }
}
std::uint64_t MetaPanel::PlayerViewRevision()
{
    const auto Data = PlayerAnalysis::Get(); return Data.Revision ^ (PlayerViewVersion << 32) ^ Data.Account ^ (MetaData::Get().Revision << 16);
}
void MetaPanel::RenderPlayerAnalysis(const Panel& Parent)
{
    auto Input = Parent.FindChildTraverse("OvrEmbeddedAccountInput");
    if (auto E = Input.Handle()) { auto V = V8::Get(Panorama::Isolate(), E, Panorama::Context(), "text"); if (V) AnalysisInput = V8::ToString(Panorama::Isolate(), V); } Input.Release();
    auto Content = Parent.FindChild("OvrEmbeddedPlayerAnalysis"); if (Content.IsValid()) Content.Delete(); Content.Release();
    Content = Panel::Create("Panel", Parent, "OvrEmbeddedPlayerAnalysis");
    Content.SetStyle({{"width", "100%"}, {"height", "fill-parent-flow(1)"}, {"flow-children", "down"}});
    MetaData::Request(); AnalyzePlayers(Content, false, true); Content.Release();
}
void MetaPanel::Update()
{
    if (!LocalPlus::Enabled() || GetTickCount64() < Next) return;
    Next = GetTickCount64() + 700;
    // Never leave our modal over the native postgame page.
    if (C_DOTAGamerules::CurrentState() == EGameState::PostGame)
    {
        Open = false;
        for (const auto Kind : {Panorama::RootKind::Dashboard, Panorama::RootKind::Hud})
        {
            Panorama::Scope Scope(Kind); if (!Scope.IsValid()) continue;
            auto Root = Panel::Root();
            auto Window = Root.FindChild("OvrMetaWindow"); Window.SetVisible(false); Window.Release();
            auto Entry = Root.FindChild("OvrMetaEntry"); Entry.SetVisible(false); Entry.Release();
            Root.Release();
        }
        return;
    }
    {
        Panorama::Scope Scope; if (Scope.IsValid())
        {
            auto Root = Panel::Root(), Entry = Root.FindChild("OvrMetaEntry");
            if (!Entry.IsValid()) { Button(Root, "OvrMetaEntry", "Доп. статистика", Toggle, 0); Entry.Release(); Entry = Root.FindChild("OvrMetaEntry"); Entry.SetStyle({{"ignore-parent-flow", "true"}, {"margin-top", "84px"}, {"margin-left", "20px"}, {"z-index", "2000"}}); }
            Entry.SetVisible(true);
            Entry.Release();
            if (Hud || !Open) { auto Old = Root.FindChild("OvrMetaWindow"); Old.SetVisible(false); Old.Release(); } Root.Release();
        }
    }
    Panorama::Scope Scope(Hud ? Panorama::RootKind::Hud : Panorama::RootKind::Dashboard); if (!Scope.IsValid()) return;
    auto Root = Panel::Root();
    if (!Root.IsValid()) { Root.Release(); Panorama::InvalidateRoot(Hud ? Panorama::RootKind::Hud : Panorama::RootKind::Dashboard); return; }
    auto Window = Root.FindChild("OvrMetaWindow");
    if (!Open) { Window.SetVisible(false); Window.Release(); Root.Release(); return; }
    BuildData::Request(Selected);
    if (Tab <= 1)
    {
        const auto Detail = MetaData::GetDetail();
        if (!Detail.Busy && Detail.HeroId != Selected) MetaData::RequestDetail(Selected);
    }
    if (Tab == 3 && RequestedAnalysis)
    {
        const auto Analysis = PlayerAnalysis::Get();
        if (!Analysis.Busy && Analysis.Account != RequestedAnalysis) PlayerAnalysis::Request(RequestedAnalysis);
    }
    if (!Window.IsValid())
    {
        Window.Release(); Window = Panel::Create("Panel", Root, "OvrMetaWindow");
        Window.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "center"}, {"vertical-align", "center"}, {"width", "92%"}, {"max-width", "1500px"}, {"height", "80%"}, {"max-height", "850px"}, {"flow-children", "down"}, {"background-color", "#182128ff"}, {"border", "1px solid #6b6b51"}, {"z-index", "3000"}}); Window.SetBool("hittest", true); Window.SetBool("hittestchildren", true);
        auto Header = Row(Window, "Header"); Header.SetStyle({{"padding", "10px"}, {"background-color", "#243139"}});
        Button(Header, "Players", "Игроки", SetTab, 3); Button(Header, "Vision", "Карта и лес", SetTab, 4); Button(Header, "Settings", "Настройки", SetTab, 5); Button(Header, "Close", "Закрыть", Close); Header.Release(); Dirty = true;
    }
    Window.SetVisible(true);
    const auto Data = MetaData::Get(); const auto Detail = MetaData::GetDetail();
    const auto Player = PlayerAnalysis::Get(); const auto Build = BuildData::Get(Selected);
    const auto Vision = VisionTracker::Get();
    const auto MapStatus = MinimapOverlay::Status();
    if (Dirty || Data.Revision != LastRevision || Detail.Revision != LastDetail || Player.Revision != LastPlayer || LastBuildBusy != Build.Busy || LastBuildHero != Selected || (Tab == 4 && (LastVisionTime != Vision.Time || LastMapStatus != MapStatus)))
    {
        Render(Window, Data); Dirty = false; LastRevision = Data.Revision; LastDetail = Detail.Revision; LastPlayer = Player.Revision; LastBuildBusy = Build.Busy; LastBuildHero = Selected; LastVisionTime = Vision.Time; LastMapStatus = MapStatus;
    }
    Window.Release(); Root.Release();
}
