#include "AssistantHud.h"
#include "AssistantData.h"
#include "AssistantMetrics.h"
#include "HudDrag.h"
#include "AssistantPlayerData.h"
#include "LocalPlus.h"
#include "HeroProgress.h"
#include "HeroProgressPlan.h"
#include "Hooks/HeroBadgeHook.h"
#include "SDK/GameRules/C_DOTAGamerules.h"
#include "MetaPanel.h"
#include "Changer/Items/FakeLoadout.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include "SDK/Files/Paths.h"
#include "SDK/Logger/Logger.h"
#include "SDK/Panorama/Panorama.h"
#include "SDK/Panorama/Panel.h"
#include "SDK/Econ/CEconItemDefinition.h"
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <map>
#include <sstream>
#include <unordered_map>

namespace
{
    constexpr const char* PanelId = "OvrLocalAssistant";
    unsigned CurrentHero = 0, TierIndex = 5, VoicePage = 0;
    bool Collapsed = false, HeaderHovered = false;
    unsigned SelectedTab = 0;
    HudDrag::State Drag;
    std::map<unsigned, unsigned> HeroBadges;
    constexpr const char* TierNames[] = {"Бронза", "Серебро", "Золото", "Платина", "Мастер", "Грандмастер"};
    constexpr const char* TierColors[] = {"#ad8973", "#aab9c6", "#d0b970", "#8fc7c0", "#bd9bce", "#db897c"};
    std::filesystem::path PreferencePath()
    {
        HMODULE Module = nullptr;
        wchar_t Path[32768]{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&PreferencePath), &Module) ||
            !GetModuleFileNameW(Module, Path, 32768)) return {};
        return std::filesystem::path(Path).parent_path() / L"ovr_hud.cfg";
    }
    void SavePreferences()
    {
        const auto Path = PreferencePath(); if (Path.empty()) return;
        auto Temporary = Path; Temporary += L".tmp";
        std::ofstream File(Temporary, std::ios::trunc);
        if (!File) return;
        File << "position " << Drag.X << ' ' << Drag.Y << '\n'
             << "tab " << SelectedTab << '\n' << "collapsed " << Collapsed << '\n';
        for (const auto& [Hero, Badge] : HeroBadges) File << "badge " << Hero << ' ' << Badge << '\n';
        File.close();
        if (File) MoveFileExW(Temporary.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    void LoadPreferences()
    {
        static const bool Loaded = [] {
            std::ifstream File(PreferencePath()); std::string Line;
            while (std::getline(File, Line))
            {
                std::istringstream Row(Line); std::string Key; Row >> Key;
                if (Key == "position") { double X, Y; if (Row >> X >> Y && std::isfinite(X) && std::isfinite(Y)) { Drag.X = X; Drag.Y = Y; } }
                else if (Key == "tab") { unsigned Tab; if (Row >> Tab && Tab < 3) SelectedTab = Tab; }
                else if (Key == "collapsed") { int Value; if (Row >> Value) Collapsed = Value == 1; }
                else if (Key == "badge") { unsigned Hero, Badge; if (Row >> Hero >> Badge && Hero > 0 && Hero < 65536 && Badge < 6) HeroBadges[Hero] = Badge; }
            }
            HeroProgress::ImportPreviewChoices(HeroBadges);
            return true;
        }();
        (void)Loaded;
    }
    ULONGLONG NextUpdate = 0, LastVoice = 0;
    bool& PublicSuggestions()
    {
        static bool Enabled = [] {
            std::ifstream File(Paths::Resolve("ovr_plus.cfg"));
            std::string Line;
            while (std::getline(File, Line))
                if (Line == "public_suggestions=1" || Line == "public_suggestions=1\r") return true;
            return false;
        }();
        return Enabled;
    }
    struct Voice { std::string Label, Sound; int Persona; };
    std::vector<Voice> ActiveVoices;
    std::string VoiceStatus = "Нажми фразу для локального прослушивания";

    V8::Local Object(const char* Name) { return V8::Get(Panorama::Isolate(), Panorama::Global(), Panorama::Context(), Name); }
    double Number(V8::Local Value, double Fallback = -1)
    {
        if (!Value || V8::TypeOf(Panorama::Isolate(), Value) != "number") return Fallback;
        const double N = V8::NumberValue(Value, Panorama::Context()); return std::isfinite(N) ? N : Fallback;
    }
    double CallNumber(const char* Owner, const char* Method, int Argc = 0, V8::Local* Args = nullptr)
    { return Number(Panorama::Invoke(Object(Owner), Method, Argc, Args)); }
    std::string Localize(const std::string& Text)
    {
        V8::Local Args[] = { Panorama::String(Text.c_str()) };
        auto Value = Panorama::Invoke(Panorama::Dollar(), "Localize", 1, Args);
        return Value ? V8::ToString(Panorama::Isolate(), Value) : Text;
    }
    void SetText(const Panel& Parent, const char* Id, const std::string& Text, const char* Color = "#e4e9e5")
    {
        auto Label = Parent.FindChildTraverse(Id); Label.SetText(Text.c_str()); Label.SetStyle("color", Color); Label.Release();
    }
    void Label(const Panel& Parent, const char* Id, const char* Text, const char* Size = "14px")
    {
        auto L = Panel::Create("Label", Parent, Id);
        L.SetStyle({{"font-size", Size}, {"font-family", "Radiance"}, {"color", "#e4e9e5"}, {"width", "100%"}, {"white-space", "normal"}, {"margin-bottom", "4px"}});
        L.SetBool("hittest", false); L.SetText(Text); L.Release();
    }
    void Collapse(std::intptr_t) { Collapsed = !Collapsed; NextUpdate = 0; SavePreferences(); Log("Assistant click: collapse=%d", Collapsed); }
    void Tab(std::intptr_t Value) { if (Value < 0 || Value >= 3) return; SelectedTab = static_cast<unsigned>(Value); NextUpdate = 0; SavePreferences(); }
    void ResetPosition(std::intptr_t) { Drag.X = 16; Drag.Y = 160; Drag.Active = false; NextUpdate = 0; SavePreferences(); }
    void HeaderHover(std::intptr_t Value)
    {
        HeaderHovered = Value != 0;
        if (HeaderHovered) { V8::Local Args[] = {Panorama::Number(0)}; auto Down = Panorama::Invoke(Object("GameUI"), "IsMouseDown", 1, Args); Drag.PreviousDown = Down && V8::BooleanValue(Panorama::Isolate(), Down); }
    }
    void NextVoice(std::intptr_t) { ++VoicePage; NextUpdate = 0; Log("Assistant click: voice page=%u", VoicePage); }
    void ToggleSuggestions(std::intptr_t) { PublicSuggestions() = !PublicSuggestions(); NextUpdate = 0; Log("Assistant click: public suggestions=%d", PublicSuggestions()); }
    void PlayVoice(std::intptr_t Index)
    {
        const auto Now = GetTickCount64();
        if (LastVoice && Now - LastVoice < 2500) { VoiceStatus = "Подожди 2,5 секунды между фразами"; NextUpdate = 0; return; }
        const std::size_t Position = VoicePage * 3 + static_cast<std::size_t>(Index);
        if (Position >= ActiveVoices.size()) { VoiceStatus = "Для этой фразы нет данных"; NextUpdate = 0; return; }
        LastVoice = Now;
        // Panorama's UI sound event also works outside custom-game Game APIs.
        V8::Local Args[] = { Panorama::String("PlaySoundEffect"), Panorama::String(ActiveVoices[Position].Sound.c_str()) };
        Panorama::Invoke(Panorama::Dollar(), "DispatchEvent", 2, Args);
        VoiceStatus = "Запрос звука: " + ActiveVoices[Position].Sound;
        Log("Assistant click: local sound requested %s", ActiveVoices[Position].Sound.c_str());
        NextUpdate = 0;
    }
    void Button(const Panel& Parent, const char* Id, const char* Text, Panorama::Handler Callback, std::intptr_t Argument = 0)
    {
        auto B = Panel::Create("Button", Parent, Id);
        B.SetBool("hittest", true);
        B.SetBool("hittestchildren", false);
        B.SetStyle({{"width", "100%"}, {"padding", "7px 9px"}, {"background-color", "#2d3c45"}, {"margin-bottom", "3px"}});
        Label(B, (std::string(Id) + "Text").c_str(), Text, "12px");
        B.SetEvent("onactivate", Callback, Argument); B.Release();
    }
    void SectionTitle(const Panel& Parent, const char* Id, const char* Text)
    {
        Label(Parent, Id, Text, "17px");
        auto P = Parent.FindChildTraverse(Id); P.SetStyle({{"color", "#c4aa73"}, {"margin-top", "10px"}, {"margin-bottom", "8px"}}); P.Release();
    }
    void Build(const Panel& Root)
    {
        auto Box = Panel::Create("Panel", Root, PanelId);
        Box.SetStyle({{"ignore-parent-flow", "true"}, {"z-index", "1000"}, {"horizontal-align", "left"}, {"vertical-align", "top"}, {"width", "340px"}, {"max-height", "620px"}, {"flow-children", "down"}, {"background-color", "#182128fa"}, {"border", "1px solid #3d4d55"}, {"border-radius", "5px"}, {"box-shadow", "0px 6px 20px #0009"}});
        Box.SetBool("hittest", true); Box.SetBool("hittestchildren", true);
        auto Header = Panel::Create("Panel", Box, "OvrHeader");
        Header.SetStyle({{"width", "100%"}, {"height", "44px"}, {"flow-children", "right"}, {"background-color", "#243139"}, {"border-bottom", "1px solid #c4aa7366"}});
        auto Grip = Panel::Create("Panel", Header, "OvrDragGrip");
        Grip.SetStyle({{"width", "fill-parent-flow(1)"}, {"height", "100%"}, {"padding", "10px 12px"}, {"flow-children", "right"}});
        Grip.SetBool("hittest", true); Grip.SetBool("hittestchildren", false);
        Label(Grip, "OvrTitle", "⠿  OVR", "20px");
        Grip.SetEvent("onmouseover", HeaderHover, 1); Grip.SetEvent("onmouseout", HeaderHover, 0); Grip.Release();
        Button(Header, "OvrCollapse", "−", Collapse);
        auto Fold = Header.FindChildTraverse("OvrCollapse"); Fold.SetStyle({{"width", "38px"}, {"height", "36px"}, {"margin-top", "4px"}, {"margin-right", "4px"}, {"padding", "5px 10px"}}); Fold.Release(); Header.Release();
        auto Body = Panel::Create("Panel", Box, "OvrAssistantBody");
        Body.SetStyle({{"width", "100%"}, {"flow-children", "down"}});
        auto Tabs = Panel::Create("Panel", Body, "OvrTabs"); Tabs.SetStyle({{"width", "100%"}, {"height", "39px"}, {"flow-children", "right"}, {"padding", "3px"}});
        const char* TabNames[] = {"Матч", "Сборки", "Герой"};
        for (unsigned I = 0; I < 3; ++I)
        {
            const auto Id = "OvrTab" + std::to_string(I);
            Button(Tabs, Id.c_str(), TabNames[I], Tab, I);
            auto B = Tabs.FindChildTraverse(Id.c_str()); B.SetStyle({{"width", "33.333%"}, {"height", "33px"}, {"padding", "5px 9px"}}); B.Release();
        }
        Tabs.Release();
        auto Match = Panel::Create("Panel", Body, "OvrPage0");
        auto Builds = Panel::Create("Panel", Body, "OvrPage1");
        auto Hero = Panel::Create("Panel", Body, "OvrPage2");
        for (auto* P : {&Match, &Builds, &Hero}) P->SetStyle({{"width", "100%"}, {"max-height", "450px"}, {"padding", "0px 12px 12px 12px"}, {"flow-children", "down"}, {"overflow", "squish scroll"}});
        SectionTitle(Match, "OvrFarmTitle", "Фарм и темп игры");
        Label(Match, "OvrDataSource", "Сравнение со средними Valve по всем рангам", "11px");
        Label(Match, "OvrLH", "Добивания: нет данных", "16px"); Label(Match, "OvrDenies", "Денаи: нет данных", "16px"); Label(Match, "OvrNW", "Стоимость: нет данных", "16px");
        SectionTitle(Match, "OvrDamageTitle", "Полученный урон");
        Label(Match, "OvrDamage", "Нет данных", "14px");
        SectionTitle(Builds, "OvrBuildTitle", "Открытые сборки");
        Label(Builds, "OvrBuildDescription", "Предметы по частоте выбора и пример прокачки из публичного матча. Источник: OpenDota.", "12px");
        Button(Builds, "OvrSuggestionsToggle", "Показать подсказки OpenDota", ToggleSuggestions);
        SectionTitle(Builds, "OvrItemsTitle", "Популярные предметы");
        for (int Index = 0; Index < 4; ++Index)
        {
            auto Row = Panel::Create("Panel", Builds, ("OvrItemRow" + std::to_string(Index)).c_str());
            Row.SetStyle({{"width", "100%"}, {"flow-children", "right"}, {"min-height", "38px"}, {"padding", "4px 0px"}});
            auto Icon = Panel::Create("DOTAItemImage", Row, ("OvrItemIcon" + std::to_string(Index)).c_str());
            Icon.SetStyle({{"width", "42px"}, {"height", "30px"}, {"margin-right", "8px"}}); Icon.Release();
            Label(Row, ("OvrItemText" + std::to_string(Index)).c_str(), "", "14px");
            auto Name = Row.FindChildTraverse(("OvrItemText" + std::to_string(Index)).c_str()); Name.SetStyle("width", "fill-parent-flow(1)"); Name.Release(); Row.Release();
        }
        Label(Builds, "OvrItemStatus", "", "11px");
        Label(Builds, "OvrSkill", "Навыки: ожидаем данные", "14px"); Label(Builds, "OvrSkillSource", "", "11px");
        Label(Builds, "OvrLimit", "Это открытая статистика, а не рекомендации Dota Plus Guide.", "11px");
        SectionTitle(Hero, "OvrLocalTier", "Локальный тир героя");
        Label(Hero, "OvrBadgeLimit", "Уровень настраивается на странице героя под его именем.", "12px");
        auto Badge = Panel::Create("Image", Hero, "OvrBadge"); Badge.SetStyle({{"width", "76px"}, {"height", "76px"}, {"horizontal-align", "center"}, {"margin", "6px 0px"}}); Badge.Release();
        SectionTitle(Hero, "OvrVoiceInfo", "Прослушивание фраз");
        Label(Hero, "OvrVoiceLimit", "Звук только у тебя, без отправки в чат.", "11px");
        for (int Index = 0; Index < 3; ++Index) Button(Hero, ("OvrVoice" + std::to_string(Index)).c_str(), "", PlayVoice, Index);
        Button(Hero, "OvrVoiceNext", "Следующие фразы", NextVoice);
        Label(Hero, "OvrVoiceStatus", "", "11px");
        Match.Release(); Builds.Release(); Hero.Release();
        Label(Body, "OvrMoveHint", "Перетащи окно за заголовок", "11px");
        auto Hint = Body.FindChildTraverse("OvrMoveHint"); Hint.SetStyle({{"color", "#91a3ab"}, {"margin-left", "12px"}, {"margin-bottom", "9px"}}); Hint.Release();
        Button(Body, "OvrResetPosition", "Сбросить положение", ResetPosition);
        auto Reset = Body.FindChildTraverse("OvrResetPosition"); Reset.SetStyle({{"background-color", "#202c34"}, {"height", "28px"}, {"padding", "3px 12px"}}); Reset.Release();
        Button(Body, "OvrOpenMeta", "Публичная статистика игроков", MetaPanel::Toggle, 1);
        Body.Release(); Box.Release(); Log("OVR HUD v2 created");
    }
    void Position(const Panel& Root, const Panel& Box)
    {
        const auto Scale = Root.Number("actualuiscale_x");
        if (!std::isfinite(Scale) || Scale <= 0) return;
        const auto YScale = Root.Number("actualuiscale_y") > 0 ? Root.Number("actualuiscale_y") : Scale;
        const auto Width = Root.Number("actuallayoutwidth") / Scale;
        const auto Height = Root.Number("actuallayoutheight") / YScale;
        if (Width <= 0 || Height <= 0) return;
        const auto BoxHeight = Box.Number("actuallayoutheight") / YScale;
        auto Cursor = Panorama::Invoke(Object("GameUI"), "GetCursorPosition");
        V8::Local Args[] = {Panorama::Number(0)};
        auto DownValue = Panorama::Invoke(Object("GameUI"), "IsMouseDown", 1, Args);
        const bool Down = DownValue && V8::BooleanValue(Panorama::Isolate(), DownValue);
        bool Released = false;
        if (Cursor && V8::IsObject(Cursor) && V8::ArrayLength(Cursor) >= 2)
        {
            const auto X = Number(V8::GetIndex(Cursor, Panorama::Context(), 0), NAN) / Scale;
            const auto Y = Number(V8::GetIndex(Cursor, Panorama::Context(), 1), NAN) / YScale;
            Released = Drag.Tick(X, Y, Down, HeaderHovered, Width, Height, 340, BoxHeight > 0 ? BoxHeight : 44);
        }
        else
        {
            Released = Drag.Active; Drag.Active = false; Drag.PreviousDown = Down;
            Drag.Clamp(Width, Height, 340, BoxHeight > 0 ? BoxHeight : 44);
        }
        char XText[32], YText[32];
        std::snprintf(XText, sizeof(XText), "%.0fpx", Drag.X); std::snprintf(YText, sizeof(YText), "%.0fpx", Drag.Y);
        Box.SetStyle("margin-left", XText); Box.SetStyle("margin-top", YText);
        if (Released) { SavePreferences(); Log("OVR HUD moved: %.0f %.0f", Drag.X, Drag.Y); }
    }
    void View(const Panel& Box)
    {
        auto Body = Box.FindChildTraverse("OvrAssistantBody"); Body.SetVisible(!Collapsed); Body.Release();
        SetText(Box, "OvrCollapseText", Collapsed ? "+" : "−");
        for (unsigned I = 0; I < 3; ++I)
        {
            auto Page = Box.FindChildTraverse(("OvrPage" + std::to_string(I)).c_str()); Page.SetVisible(I == SelectedTab); Page.Release();
            auto Tab = Box.FindChildTraverse(("OvrTab" + std::to_string(I)).c_str());
            Tab.SetStyle("background-color", I == SelectedTab ? "#3b4d55" : "#202c34");
            Tab.SetStyle("border-bottom", I == SelectedTab ? "2px solid #c4aa73" : "2px solid #202c34"); Tab.Release();
        }
    }
    std::unordered_map<unsigned, std::vector<Voice>> LoadVoices()
    {
        std::ifstream File(Paths::Resolve("assistant/voice_lines.json")); std::stringstream Text; Text << File.rdbuf();
        google::protobuf::Struct Json;
        if (!google::protobuf::util::JsonStringToMessage(Text.str(), &Json).ok()) return {};
        std::unordered_map<unsigned, std::vector<Voice>> Result;
        for (const auto& [Hero, List] : Json.fields())
        {
            unsigned HeroId = 0;
            auto Parsed = std::from_chars(Hero.data(), Hero.data()+Hero.size(), HeroId);
            if (Parsed.ec != std::errc() || Parsed.ptr != Hero.data()+Hero.size() || !HeroId) continue;
            for (const auto& V : List.list_value().values())
            {
                const auto& Fields = V.struct_value().fields();
                auto S = [&](const char* Key) { auto It = Fields.find(Key); return It == Fields.end() ? std::string() : It->second.string_value(); };
                auto P = Fields.find("persona"); int Persona = P == Fields.end() ? 0 : static_cast<int>(P->second.number_value());
                if (!S("sound").empty()) Result[HeroId].push_back({S("label"), S("sound"), Persona});
            }
        }
        return Result;
    }
    void Metric(const Panel& Box, const char* Id, const char* Title, double Actual, double Target)
    {
        if (Actual < 0) { SetText(Box, Id, std::string(Title) + ": нет данных", "#a5b2c2"); return; }
        const auto Delta = Actual - Target;
        const bool Equal = std::abs(Delta) <= std::max(1., Target * .025);
        char Text[160]; std::snprintf(Text, sizeof(Text), "%s %s %.0f / %.0f", Equal ? "•" : Delta > 0 ? "▲" : "▼", Title, Actual, Target);
        SetText(Box, Id, Text, Equal ? "#a5b2c2" : Delta > 0 ? "#80d6ab" : "#ee9299");
    }
}

void AssistantHud::Update()
{
    if (!LocalPlus::Enabled()) return;
    const auto Now = GetTickCount64();
    if (Now < NextUpdate && !HeaderHovered && !Drag.Active) return;
    LoadPreferences();
    auto* Hero = C_DOTAPlayerController::LocalHero();
    Panorama::Scope Scope(Panorama::RootKind::Hud); if (!Scope.IsValid()) { NextUpdate = Now + 700; return; }
    auto Root = Panel::Root();
    if (!Root.IsValid()) { Root.Release(); Panorama::InvalidateRoot(Panorama::RootKind::Hud); return; }
    auto Box = Root.FindChildTraverse(PanelId);
    if (!Hero || Hero->IsIllusion() || C_DOTAGamerules::CurrentState() == EGameState::PostGame)
    {
        NextUpdate = Now + 700;
        if (Drag.Active) { Drag.Active = false; SavePreferences(); }
        HeaderHovered = false;
        Box.SetVisible(false); Box.Release(); Root.Release(); AssistantData::SetHero(0); return;
    }
    if (!Box.IsValid()) { Box.Release(); Build(Root); Box = Root.FindChildTraverse(PanelId); }
    Box.SetVisible(true);
    Position(Root, Box);
    if (Now < NextUpdate) { Box.Release(); Root.Release(); return; }
    NextUpdate = Now + 700;
    View(Box);
    const unsigned HeroId = Hero->HeroId();
    if (CurrentHero != HeroId)
    {
        CurrentHero = HeroId; VoicePage = 0;
        TierIndex = HeroProgressPlan::Index(HeroProgress::Level(HeroId));
    }
    AssistantData::SetHero(HeroId);
    if (Collapsed) { Box.Release(); Root.Release(); return; }
    const int Player = Hero->PlayerID(), Entity = Hero->Index();
    if (Player < 0 || Player >= 24) { Box.SetVisible(false); Box.Release(); Root.Release(); return; }
    V8::Local PlayerArgs[] = {Panorama::Number(Player)};
    V8::Local TimeArgs[] = {Panorama::Number(0), Panorama::Number(0)};
    const double Time = CallNumber("Game", "GetDOTATime", 2, TimeArgs);
    const auto Data = AssistantData::Get();
    const auto PlayerData = AssistantPlayerData::Read(Player);
    std::vector<AssistantMetrics::Point> Points;
    if (Data.HeroId == HeroId) for (const auto& P : Data.TimedStats) Points.push_back({static_cast<double>(P.Time), P.LastHits, P.Denies, P.NetWorth});
    auto Target = AssistantMetrics::At(Points, Time);
    if (Target)
    {
        SetText(Box, "OvrFarmTitle", "Текущее / среднее Valve, все ранги");
        Metric(Box, "OvrLH", "Добивания", CallNumber("Players", "GetLastHits", 1, PlayerArgs), Target->LastHits);
        Metric(Box, "OvrDenies", "Денаи", CallNumber("Players", "GetDenies", 1, PlayerArgs), Target->Denies);
        Metric(Box, "OvrNW", "Стоимость", PlayerData.NetWorthAvailable ? PlayerData.NetWorth : -1, Target->NetWorth);
    }
    else
    {
        SetText(Box, "OvrFarmTitle", Points.empty() ? Data.TimedStatsStatus : "Текущее время вне диапазона базы Valve", "#a5b2c2");
        SetText(Box, "OvrLH", "Добивания: " + std::to_string(static_cast<int>(CallNumber("Players", "GetLastHits", 1, PlayerArgs))));
        SetText(Box, "OvrDenies", "Денаи: " + std::to_string(static_cast<int>(CallNumber("Players", "GetDenies", 1, PlayerArgs))));
        SetText(Box, "OvrNW", PlayerData.NetWorthAvailable ? "Стоимость: " + std::to_string(PlayerData.NetWorth) : "Стоимость: нет данных");
    }
    if (PlayerData.DamageAvailable && PlayerData.Physical + PlayerData.Magical + PlayerData.Pure > 0)
    {
        const auto Sum = PlayerData.Physical + PlayerData.Magical + PlayerData.Pure;
        char Text[200]; std::snprintf(Text, sizeof(Text), "Полученный урон: физ. %.0f%% / маг. %.0f%% / чистый %.0f%%", 100*PlayerData.Physical/Sum, 100*PlayerData.Magical/Sum, 100*PlayerData.Pure/Sum); SetText(Box, "OvrDamage", Text);
    }
    else SetText(Box, "OvrDamage", "Полученный урон: нет данных", "#a5b2c2");

    const auto& Items = Time < 0 ? Data.StartingItems : Time < 600 ? Data.EarlyItems : Time < 1500 ? Data.MidItems : Data.LateItems;
    SetText(Box, "OvrItemsTitle", Time < 0 ? "Стартовые предметы" : Time < 600 ? "Ранняя игра: популярные предметы" : Time < 1500 ? "Середина игры: популярные предметы" : "Поздняя игра: популярные предметы");
    unsigned Visible = 0;
    if (Data.HeroId == HeroId) for (const auto& Item : Items)
    {
        V8::Local Args[] = {Panorama::Number(Entity), Panorama::String(Item.Name.c_str())};
        const auto Owned = Panorama::Invoke(Object("Entities"), "HasItemInInventory", 2, Args);
        if (Owned && V8::BooleanValue(Panorama::Isolate(), Owned)) continue;
        if (Visible >= 4) break;
        auto Row = Box.FindChildTraverse(("OvrItemRow" + std::to_string(Visible)).c_str()); Row.SetVisible(true); Row.Release();
        auto Icon = Box.FindChildTraverse(("OvrItemIcon" + std::to_string(Visible)).c_str());
        if (auto Handle = Icon.Handle()) V8::Set(Handle, Panorama::Context(), Panorama::String("itemname"), Panorama::String(Item.Name.c_str())); Icon.Release();
        auto Name = Localize("#DOTA_Tooltip_ability_" + Item.Name);
        SetText(Box, ("OvrItemText" + std::to_string(Visible)).c_str(), Name); ++Visible;
    }
    for (unsigned Index = Visible; Index < 4; ++Index) { auto Row = Box.FindChildTraverse(("OvrItemRow" + std::to_string(Index)).c_str()); Row.SetVisible(false); Row.Release(); }
    SetText(Box, "OvrItemStatus", Data.HeroId == HeroId ? Data.ItemsStatus : "Запрашиваем список предметов…", "#a5b2c2");
    std::unordered_map<std::string, unsigned> Expected;
    std::string Suggested;
    if (Data.HeroId == HeroId) for (const auto& Name : Data.SkillOrder)
    {
        const unsigned Want = ++Expected[Name];
        V8::Local NameArgs[] = {Panorama::Number(Entity), Panorama::String(Name.c_str())};
        auto Ability = Panorama::Invoke(Object("Entities"), "GetAbilityByName", 2, NameArgs);
        const double AbilityIndex = Number(Ability); if (AbilityIndex < 0) continue;
        V8::Local A[] = {Panorama::Number(AbilityIndex)};
        const auto CanLearn = Panorama::Invoke(Object("Abilities"), "CanLearn", 1, A);
        if (CallNumber("Abilities", "GetLevel", 1, A) < Want && CanLearn && V8::BooleanValue(Panorama::Isolate(), CanLearn)) { Suggested = Localize("#DOTA_Tooltip_ability_" + Name); break; }
    }
    SetText(Box, "OvrSkill", Suggested.empty() ? "Навыки: нет доступного улучшения" : "Навык по примеру матча: " + Suggested);
    SetText(Box, "OvrSkillSource", Data.SkillOrder.empty() ? Data.SkillsStatus : "Пример открытого матча №" + std::to_string(Data.SourceMatchId) + "; не универсальная сборка", "#a5b2c2");
    SetText(Box, "OvrSuggestionsToggleText", PublicSuggestions() ? "Скрыть подсказки OpenDota" : "Показать подсказки OpenDota");
    // Public examples are opt-in; never present them as the native Valve assistant.
    for (const char* Id : {"OvrItemsTitle", "OvrItemStatus", "OvrSkill", "OvrSkillSource", "OvrLimit"})
    {
        auto P = Box.FindChildTraverse(Id);
        P.SetVisible(PublicSuggestions());
        P.Release();
    }
    if (!PublicSuggestions()) for (unsigned I = 0; I < 4; ++I)
    {
        auto P = Box.FindChildTraverse(("OvrItemRow" + std::to_string(I)).c_str()); P.SetVisible(false); P.Release();
    }
    auto Badge = Box.FindChildTraverse("OvrBadge");
    const auto SelectedLevel = HeroProgress::Level(HeroId);
    Badge.SetVisible(SelectedLevel != 0);
    Badge.SetImage(("s2r://panorama/images/hero_badges/hero_badge_rank_" + std::to_string(TierIndex) + "_png.vtex").c_str()); Badge.Release();
    SetText(Box, "OvrLocalTier", SelectedLevel ? std::string("Локально: ") + TierNames[TierIndex] + " · " + std::to_string(SelectedLevel) : "Настоящий уровень Dota", TierColors[TierIndex]);
    SetText(Box, "OvrBadgeLimit", HeroBadgeHook::Ready() ? "Настройка уровня — на странице героя, под именем. Выбор сохраняется для каждого героя." : "Нативный обработчик тира недоступен в этой версии клиента.", "#a5b2c2");
    SetText(Box, "OvrVoiceStatus", VoiceStatus, "#a5b2c2");

    static const auto Voices = LoadVoices();
    bool Persona = false;
    const auto ModelSource = FakeLoadout::FindSource("entity_model", Hero->DesignerName());
    if (ModelSource.Definition) for (const auto& M : ModelSource.Definition->AssetModifiers(ModelSource.Style)) Persona |= M.Type == "persona";
    ActiveVoices.clear();
    if (auto Found = Voices.find(HeroId); Found != Voices.end()) for (const auto& V : Found->second) if (V.Persona == (Persona ? 1 : 0)) ActiveVoices.push_back(V);
    const auto PageCount = std::max<std::size_t>(1, (ActiveVoices.size()+2)/3); VoicePage %= static_cast<unsigned>(PageCount);
    for (unsigned Index = 0; Index < 3; ++Index)
    {
        const auto Position = VoicePage*3 + Index;
        auto B = Box.FindChildTraverse(("OvrVoice" + std::to_string(Index)).c_str()); B.SetVisible(Position < ActiveVoices.size()); B.Release();
        if (Position < ActiveVoices.size()) SetText(Box, ("OvrVoice" + std::to_string(Index) + "Text").c_str(), Localize(ActiveVoices[Position].Label));
    }
    Box.Release(); Root.Release();
}
