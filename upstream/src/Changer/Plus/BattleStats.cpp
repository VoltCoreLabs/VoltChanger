#include "BattleStats.h"
#include "BattleData.h"
#include "NativeHistory.h"
#include "LocalPlus.h"
#include "MetaPanel.h"
#include "PlayerAnalysis.h"
#include "SDK/Panorama/Panel.h"
#include "SDK/SteamAPI/ISteamUser.h"
#include "SDK/Logger/Logger.h"
#include <windows.h>
#include <algorithm>
#include <ctime>

namespace
{
    ULONGLONG Next = 0;
    std::uint64_t Painted = 0;
    unsigned Days = 0, Page = 0;
    bool Dirty = true, Show = true;
    bool AnalysisMode = false;
    std::uint64_t AnalysisPainted = 0;
    void Mode(std::intptr_t Value) { AnalysisMode = Value != 0; Dirty = true; }
    void Period(std::intptr_t Value) { Days = static_cast<unsigned>(Value); Page = 0; Dirty = true; }
    void Navigate(std::intptr_t Value) { if (Value < 0) { if (Page) --Page; } else ++Page; Dirty = true; }
    void Toggle(std::intptr_t) { Show = !Show; Dirty = true; }
    void Refresh(std::intptr_t) { NativeHistory::Request(); if (auto* U = ISteamUser::Get()) BattleData::Request(U->AccountId()); }
    void Text(const Panel& P, const char* Id, const std::string& T, const char* Color = "#c8d2dc", const char* Width = "100%")
    {
        auto L = Panel::Create("Label", P, Id);
        L.SetStyle({{"font-size", "17px"}, {"color", Color}, {"width", Width}, {"margin-bottom", "6px"}, {"white-space", "normal"}});
        L.SetText(T.c_str()); L.Release();
    }
    void Button(const Panel& P, const char* Id, const char* T, Panorama::Handler H, std::intptr_t Arg)
    {
        auto B = Panel::Create("Button", P, Id);
        B.SetBool("hittest", true); B.SetBool("hittestchildren", false);
        B.SetStyle({{"width", "fit-children"}, {"min-width", "80px"}, {"height", "40px"}, {"padding", "6px 10px"}, {"margin-right", "6px"}, {"background-color", "#263844"}});
        Text(B, "Caption", T, "#c8d2dc", "fit-children"); B.SetEvent("onactivate", H, Arg); B.Release();
    }
    Panel FindPage(const Panel& Root)
    {
        auto Pages = Root.FindChildrenWithClass("DashboardPage");
        Panel Found;
        for (auto& P : Pages)
        {
            if (!Found.IsValid() && P.Type() == "DOTAProfileBattleStatsPage") { Found = P; }
            else P.Release();
        }
        return Found;
    }
    std::string Stat(int V) { return V < 0 ? "—" : std::to_string(V); }
    std::string Date(std::uint32_t Timestamp)
    {
        std::time_t T = Timestamp;
        std::tm Time{}; char Buffer[32]{};
        if (localtime_s(&Time, &T) == 0) std::strftime(Buffer, sizeof(Buffer), "%d.%m.%Y %H:%M", &Time);
        return Buffer;
    }
}

void BattleStats::Update()
{
    if (!LocalPlus::Enabled() || GetTickCount64() < Next) return;
    Next = GetTickCount64() + 750;
    Panorama::Scope Scope;
    if (!Scope.IsValid()) return;
    auto Root = Panel::Root(); auto PagePanel = FindPage(Root); Root.Release();
    if (!PagePanel.IsValid()) { PagePanel.Release(); return; }
    auto Host = PagePanel.FindChildTraverse("BattleReportStats");
    if (!Host.IsValid()) { Host.Release(); PagePanel.Release(); return; }
    auto ToggleButton = PagePanel.FindChildTraverse("OvrBattleToggle");
    if (!ToggleButton.IsValid())
    {
        Button(PagePanel, "OvrBattleToggle", "Мои матчи OVR / штатная статистика", Toggle, 0);
        ToggleButton.Release(); ToggleButton = PagePanel.FindChildTraverse("OvrBattleToggle");
        ToggleButton.SetStyle({{"ignore-parent-flow", "true"}, {"horizontal-align", "right"}, {"vertical-align", "top"}, {"margin-top", "118px"}, {"z-index", "100"}});
    }
    ToggleButton.Release();
    auto Box = Host.FindChild("OvrBattleHistory");
    if (!Show) { Box.SetVisible(false); Box.Release(); Host.Release(); PagePanel.Release(); return; }
    auto* User = ISteamUser::Get();
    if (!User) { Box.Release(); Host.Release(); PagePanel.Release(); return; }
    auto Embedded = Box.FindChild("OvrProfileAnalyzerHost");
    if (AnalysisMode && !Dirty && Embedded.IsValid())
    {
        const auto Revision = MetaPanel::PlayerViewRevision();
        if (Revision != AnalysisPainted) { MetaPanel::RenderPlayerAnalysis(Embedded); AnalysisPainted = MetaPanel::PlayerViewRevision(); }
        Embedded.Release(); Box.SetVisible(true); Box.Release(); Host.Release(); PagePanel.Release(); return;
    }
    Embedded.Release();
    auto Native = NativeHistory::Get();
    // Own authenticated history first. External public history is only fallback.
    if (PagePanel.Number("actuallayoutwidth") > 0 && Native.Account != User->AccountId())
    {
        NativeHistory::Request(); Native = NativeHistory::Get();
    }
    auto Data = Native;
    if (!Native.Busy && Native.Matches.empty())
    {
        auto Public = BattleData::Get();
        if (Public.Account != User->AccountId()) { BattleData::Request(User->AccountId()); Public = BattleData::Get(); }
        Data = Public;
        Data.Status = (Native.Status.empty() ? "Нет соединения с координатором Dota" : Native.Status) + ". " + Public.Status;
        Data.Revision = (Native.Revision << 32) | Public.Revision;
    }
    else Data.Revision = Native.Revision << 32;
    if (!Box.IsValid() || Dirty || Painted != Data.Revision)
    {
        if (Box.IsValid()) Box.Delete();
        Box.Release(); Box = Panel::Create("Panel", Host, "OvrBattleHistory");
        Box.SetStyle({{"ignore-parent-flow", "true"}, {"width", "100%"}, {"height", "100%"}, {"flow-children", "down"}, {"background-color", "#101b24ff"}, {"padding", "18px"}, {"z-index", "90"}});
        Text(Box, "Heading", "МОИ МАТЧИ · OVR", "#d8bd79");
        auto Tabs = Panel::Create("Panel", Box, "OvrProfileStatsTabs"); Tabs.SetStyle({{"width", "100%"}, {"height", "44px"}, {"flow-children", "right"}});
        Button(Tabs, "HistoryTab", "Матчи", Mode, 0); Button(Tabs, "AnalysisTab", "Пики, серии и анализ игрока", Mode, 1); Tabs.Release();
        if (AnalysisMode)
        {
            auto Analysis = Panel::Create("Panel", Box, "OvrProfileAnalyzerHost"); Analysis.SetStyle({{"width", "100%"}, {"height", "fill-parent-flow(1)"}, {"flow-children", "down"}});
            MetaPanel::RenderPlayerAnalysis(Analysis); AnalysisPainted = MetaPanel::PlayerViewRevision(); Analysis.Release();
            Dirty = false; Box.Release(); Host.Release(); PagePanel.Release(); return;
        }
        Text(Box, "Source", Data.Account == User->AccountId() ? Data.Status : "Загружаем мою публичную историю…");
        auto Controls = Panel::Create("Panel", Box, "Periods"); Controls.SetStyle({{"width", "100%"}, {"height", "46px"}, {"flow-children", "right"}});
        Button(Controls, "Week", "Неделя", Period, 7); Button(Controls, "Month", "Месяц", Period, 30);
        Button(Controls, "Quarter", "3 месяца", Period, 90); Button(Controls, "Year", "Год", Period, 365);
        Button(Controls, "All", "Все загруженные", Period, 0); Button(Controls, "Refresh", "Обновить (1 мин)", Refresh, 0); Controls.Release();
        Text(Box, "Scope", "Период: " + (Days ? std::to_string(Days) + " дней" : std::string("все загруженные")) + ". Фильтры OVR независимы от фильтров Valve. MMR не предоставляется источником.");
        std::vector<BattleData::Match> Matches;
        const auto Now = static_cast<std::uint64_t>(std::time(nullptr));
        if (Data.Account == User->AccountId()) for (const auto& M : Data.Matches)
            if (!Days || static_cast<std::uint64_t>(M.Started) + Days * 86400ull >= Now) Matches.push_back(M);
        const auto Wins = std::count_if(Matches.begin(), Matches.end(), [](const auto& M) { return M.Won; });
        Text(Box, "Summary", "В выборке: " + std::to_string(Matches.size()) + " · побед: " + std::to_string(Wins) + " · поражений: " + std::to_string(Matches.size() - Wins));
        Page = std::min(Page, Matches.empty() ? 0u : static_cast<unsigned>((Matches.size() - 1) / 10));
        auto Rows = Panel::Create("Panel", Box, "Rows"); Rows.SetStyle({{"width", "100%"}, {"height", "fill-parent-flow(1)"}, {"flow-children", "down"}, {"overflow", "squish scroll"}});
        for (unsigned I = Page * 10; I < Matches.size() && I < (Page + 1) * 10; ++I)
        {
            const auto& M = Matches[I];
            auto Row = Panel::Create("Panel", Rows, ("Match" + std::to_string(M.Id)).c_str());
            Row.SetStyle({{"width", "100%"}, {"flow-children", "right"}, {"padding", "6px"}, {"background-color", "#1c2b36"}, {"margin-bottom", "3px"}});
            auto Hero = Panel::Create("DOTAHeroImage", Row, "Hero");
            Hero.SetStyle({{"width", "64px"}, {"height", "36px"}, {"margin-right", "12px"}});
            if (auto H = Hero.Handle()) V8::Set(H, Panorama::Context(), Panorama::String("heroid"), Panorama::Number(M.Hero));
            Hero.Release();
            Text(Row, "Date", Date(M.Started), "#a5b3c0", "180px");
            Text(Row, "Result", M.Won ? "Победа" : "Поражение", M.Won ? "#8cce83" : "#e18b83", "125px");
            Text(Row, "Kda", Stat(M.Kills) + "/" + Stat(M.Deaths) + "/" + Stat(M.Assists), "#dde4ea", "120px");
            Text(Row, "Duration", M.Duration == UINT32_MAX ? "—" : std::to_string(M.Duration / 60) + ":" + (M.Duration % 60 < 10 ? "0" : "") + std::to_string(M.Duration % 60), "#a5b3c0", "80px");
            Text(Row, "Rates", "GPM " + Stat(M.Gpm) + " · XPM " + Stat(M.Xpm), "#a5b3c0", "210px");
            Text(Row, "Id", "#" + std::to_string(M.Id), "#a5b3c0", "150px"); Row.Release();
        }
        if (Matches.empty() && !Data.Busy) Text(Rows, "Empty", "В загруженной истории за выбранный период матчей нет.");
        Rows.Release();
        auto Nav = Panel::Create("Panel", Box, "Navigation"); Nav.SetStyle("flow-children", "right");
        Button(Nav, "Back", "Назад", Navigate, -1); Text(Nav, "Page", "Страница " + std::to_string(Page + 1), "#c8d2dc", "150px"); Button(Nav, "Forward", "Далее", Navigate, 1); Nav.Release();
        Dirty = false; Painted = Data.Revision;
        Log("Battle history panel updated: %zu public matches", Matches.size());
    }
    Box.SetVisible(true); Box.Release(); Host.Release(); PagePanel.Release();
}
