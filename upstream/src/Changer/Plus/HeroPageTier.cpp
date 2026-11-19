#include "HeroPageTier.h"
#include "HeroPageIdentity.h"
#include "HeroProgress.h"
#include "HeroProgressPlan.h"
#include "LocalPlus.h"
#include "SDK/Panorama/Panel.h"
#include "SDK/Logger/Logger.h"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace
{
    Panel Controls;
    unsigned Hero = 0, Draft = 30, Observed = 0;
    ULONGLONG Next = 0;
    bool Updating = false;
    std::string LastTitle, Status;
    std::map<unsigned, std::string> Names;
    bool Loaded = false;


    std::string Text(const Panel& P)
    {
        auto V = P.Handle() ? V8::Get(Panorama::Isolate(), P.Handle(), Panorama::Context(), "text") : nullptr;
        return V && V8::TypeOf(Panorama::Isolate(), V) == "string" ? V8::ToString(Panorama::Isolate(), V) : "";
    }
    void Caption(const char* Id, const std::string& Value)
    { auto P = Controls.FindChildTraverse(Id); P.SetText(Value.c_str()); P.Release(); }
    void LoadNames()
    {
        if (Loaded) return; Loaded = true;
        HMODULE Module = nullptr; wchar_t Path[32768]{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&HeroPageTier::Update), &Module) || !GetModuleFileNameW(Module, Path, 32768)) return;
        std::ifstream File(std::filesystem::path(Path).parent_path() / L"assistant" / L"hero_names.txt");
        std::string Line;
        while (std::getline(File, Line))
        {
            std::istringstream Row(Line); unsigned Id = 0; std::string Name;
            if (Row >> Id >> Name && HeroProgressPlan::Valid(Id, 0) && Name.starts_with("npc_dota_hero_")) Names[Id] = Name;
        }
    }
    unsigned Resolve(const Panel& Name)
    {
        auto Titles = Name.FindChildrenWithClass("TitleExtraLarge");
        std::string Title;
        for (auto& P : Titles) { if (Title.empty()) Title = Text(P); P.Release(); }
        if (Title.empty()) { LastTitle.clear(); return 0; }
        if (Title == LastTitle) return Hero;
        LastTitle = Title;
        // Match the exact native localized title, never the currently played hero:
        // browsing a different hero must not write the active hero's preference.
        unsigned Found = 0;
        for (const auto& [Id, Internal] : Names)
        {
            V8::Local Args[] = {Panorama::String(("#" + Internal).c_str())};
            auto Localized = Panorama::Invoke(Panorama::Dollar(), "Localize", 1, Args);
            if (Localized && V8::TypeOf(Panorama::Isolate(), Localized) == "string" && HeroPageIdentity::SameTitle(V8::ToString(Panorama::Isolate(), Localized), Title))
            { if (Found) return 0; Found = Id; }
        }
        Log("Hero page tier: native title='%s' resolved hero=%u catalog=%zu", Title.c_str(), Found, Names.size());
        return Found;
    }
    void Label(const Panel& Parent, const char* Id, const char* Value, const char* Color = "#dce2e4")
    {
        auto P = Panel::Create("Label", Parent, Id); P.SetBool("hittest", false); P.SetText(Value);
        P.SetStyle({{"font-family", "Radiance"}, {"font-size", "14px"}, {"color", Color}, {"vertical-align", "center"}}); P.Release();
    }
    void NativeVisibility(const Panel& P, bool Override, const char* Value = "collapse")
    {
        if (!P.IsValid()) return;
        constexpr const char* Key = "__ovrHeroLevelOriginalVisibility";
        auto Saved = V8::Get(Panorama::Isolate(), P.Handle(), Panorama::Context(), Key);
        const bool Stored = Saved && V8::TypeOf(Panorama::Isolate(), Saved) == "string";
        if (Override)
        {
            if (!Stored)
            {
                auto Style = V8::Get(Panorama::Isolate(), P.Handle(), Panorama::Context(), "style");
                auto Prior = Style ? V8::Get(Panorama::Isolate(), Style, Panorama::Context(), "visibility") : nullptr;
                const auto Text = Prior && V8::TypeOf(Panorama::Isolate(), Prior) == "string" ? V8::ToString(Panorama::Isolate(), Prior) : "";
                V8::Set(P.Handle(), Panorama::Context(), Panorama::String(Key), Panorama::String(Text.c_str()));
            }
            P.SetStyle("visibility", Value);
        }
        else if (Stored)
        {
            const auto Text = V8::ToString(Panorama::Isolate(), Saved);
            // Panorama rejects an empty visibility style instead of clearing it.
            // Restore the normal visible default; native ancestor rules remain.
            P.SetStyle("visibility", Text.empty() ? "visible" : Text.c_str()); P.SetBool(Key, false);
        }
    }
    void MenuBadge(const Panel& Content, unsigned Level)
    {
        auto Right = Content.FindChild("RightColumn");
        auto NativeBadge = Right.FindChild("HeroBadge");
        if (!NativeBadge.IsValid()) { NativeBadge.Release(); Right.Release(); return; }
        auto Visual = NativeBadge.FindChild("OvrHeroTierContents");
        if (Level && !Visual.IsValid())
        {
            Visual.Release(); Visual = Panel::Create("Panel", NativeBadge, "OvrHeroTierContents");
            Visual.SetBool("hittest", false); Visual.SetBool("hittestchildren", false);
            Visual.SetStyle({{"width", "112px"}, {"height", "112px"}, {"horizontal-align", "center"}, {"vertical-align", "center"}});
            auto Icon = Panel::Create("Image", Visual, "Icon"); Icon.SetBool("hittest", false);
            Icon.SetStyle({{"width", "100%"}, {"height", "100%"}}); Icon.Release();
            Label(Visual, "Level", "");
            auto Number = Visual.FindChild("Level");
            Number.SetStyle({{"horizontal-align", "center"}, {"vertical-align", "center"}, {"font-size", "29px"},
                {"font-weight", "bold"}, {"text-align", "center"}, {"color", "#ffffff"}, {"text-shadow", "0px 2px 8px 4 #000c"}}); Number.Release();
        }
        const bool Active = Level && Visual.IsValid();
        NativeVisibility(NativeBadge, Active, "visible");
        auto NativeParts = NativeBadge.Children();
        for (auto& Part : NativeParts)
        {
            if (Part.Id() != "OvrHeroTierContents") NativeVisibility(Part, Active);
            Part.Release();
        }
        // The native badge extends outside the right column. Let its ordinary
        // image child use that same space instead of clipping the whole icon.
        if (Active) Right.SetStyle("overflow", "noclip");
        Visual.SetVisible(Active);
        if (Active)
        {
            auto Icon = Visual.FindChild("Icon");
            Icon.SetImage(("s2r://panorama/images/hero_badges/hero_badge_rank_" + std::to_string(HeroProgressPlan::Index(Level)) + "_png.vtex").c_str()); Icon.Release();
            auto Number = Visual.FindChild("Level"); Number.SetText(std::to_string(Level).c_str()); Number.Release();
        }
        // XP, native progress bar and reward controls are deliberately untouched.
        Visual.Release(); NativeBadge.Release(); Right.Release();
    }
    void Button(const Panel& Parent, const char* Id, const char* Value, Panorama::Handler Callback)
    {
        auto P = Panel::Create("Button", Parent, Id); P.AddClass("ButtonPrimary"); P.AddClass("DarkGrey");
        P.SetBool("hittest", true); P.SetBool("hittestchildren", false);
        P.SetStyle({{"height", "28px"}, {"width", "fit-children"}, {"padding", "3px 9px"}, {"margin-left", "6px"}});
        Label(P, "Caption", Value); P.SetEvent("onactivate", Callback, 0); P.Release();
    }
    void Display()
    {
        Updating = true;
        auto Input = Controls.FindChildTraverse("OvrHeroLevelInput"); Input.SetText(std::to_string(Draft).c_str()); Input.Release();
        auto Slider = Controls.FindChildTraverse("OvrHeroLevelSlider");
        if (Slider.Handle()) V8::Set(Slider.Handle(), Panorama::Context(), Panorama::String("value"), Panorama::Number(Draft));
        Slider.Release();
        auto Badge = Controls.FindChildTraverse("OvrHeroLevelBadge");
        Badge.SetImage(("s2r://panorama/images/hero_badges/hero_badge_rank_" + std::to_string(HeroProgressPlan::Index(Draft)) + "_png.vtex").c_str()); Badge.Release();
        Caption("OvrHeroLevelStatus", Status);
        Updating = false;
    }
    void Slide(std::intptr_t)
    {
        if (Updating || !Hero) return;
        auto Slider = Controls.FindChildTraverse("OvrHeroLevelSlider");
        auto V = Slider.Handle() ? V8::Get(Panorama::Isolate(), Slider.Handle(), Panorama::Context(), "value") : nullptr;
        if (V && V8::TypeOf(Panorama::Isolate(), V) == "number")
        {
            const auto N = V8::NumberValue(V, Panorama::Context());
            if (std::isfinite(N)) { Draft = static_cast<unsigned>(std::clamp(std::round(N), 1., 30.)); Status = "Нажми «Применить», чтобы сохранить"; }
        }
        Slider.Release(); Display();
    }
    bool SelectionCurrent()
    {
        auto Content = Controls.Parent(); auto Name = Content.FindChild("HeroName");
        const auto Current = Resolve(Name); Name.Release(); Content.Release();
        if (Current && Current == Hero) return true;
        // Do not commit an old draft during the native page's hero transition.
        LastTitle.clear(); Next = 0; return false;
    }
    void Apply(std::intptr_t)
    {
        if (!SelectionCurrent()) return;
        auto Input = Controls.FindChildTraverse("OvrHeroLevelInput"); const auto Value = Text(Input); Input.Release();
        unsigned Level = 0; const auto P = std::from_chars(Value.data(), Value.data() + Value.size(), Level);
        if (P.ec != std::errc() || P.ptr != Value.data() + Value.size() || Level < 1 || Level > 30)
        { Caption("OvrHeroLevelStatus", "Введи целое число от 1 до 30"); return; }
        if (HeroProgress::SetLevel(Hero, Level))
        { Draft = Observed = Level; Status = "Уровень " + std::to_string(Level) + " сохранён для этого героя"; Display(); Next = 0; }
    }
    void Reset(std::intptr_t)
    {
        if (SelectionCurrent() && HeroProgress::SetLevel(Hero, 0))
        { Observed = 0; Status = "Используется настоящий уровень Dota"; Display(); Next = 0; }
    }
    void Create(const Panel& Content)
    {
        Controls.Release(); Controls = Panel::Create("Panel", Content, "OvrHeroPageTier");
        Controls.SetBool("hittest", true); Controls.SetBool("hittestchildren", true);
        Controls.SetStyle({{"ignore-parent-flow", "true"}, {"width", "500px"}, {"height", "78px"}, {"flow-children", "down"}, {"padding", "5px 8px"}, {"background-color", "#17232ce8"}, {"border-left", "2px solid #be9958"}});
        auto Row = Panel::Create("Panel", Controls, "OvrHeroLevelRow"); Row.SetStyle({{"width", "100%"}, {"height", "32px"}, {"flow-children", "right"}});
        auto Badge = Panel::Create("Image", Row, "OvrHeroLevelBadge"); Badge.SetStyle({{"width", "30px"}, {"height", "30px"}, {"margin-right", "6px"}}); Badge.Release();
        Label(Row, "OvrHeroLevelTitle", "Уровень героя");
        auto Input = Panel::Create("TextEntry", Row, "OvrHeroLevelInput"); Input.SetBool("hittest", true);
        Input.SetStyle({{"width", "44px"}, {"height", "28px"}, {"margin-left", "7px"}, {"font-size", "16px"}, {"color", "#fff"}}); Input.Release();
        Button(Row, "OvrHeroLevelApply", "Применить", Apply); Button(Row, "OvrHeroLevelReset", "Настоящий", Reset); Row.Release();
        auto Slider = Panel::CreateWithProperties("Slider", Controls, "OvrHeroLevelSlider", {{"direction", "horizontal"}, {"min", "1"}, {"max", "30"}, {"value", "30"}});
        Slider.AddClass("HorizontalSlider"); Slider.SetBool("hittest", true);
        Slider.SetStyle({{"width", "100%"}, {"height", "17px"}, {"margin", "2px 0px"}}); Slider.SetEvent("onvaluechanged", Slide, 0); Slider.Release();
        Label(Controls, "OvrHeroLevelStatus", "", "#aebbc4");
        auto Info = Controls.FindChild("OvrHeroLevelStatus"); Info.SetStyle("font-size", "11px"); Info.Release();
        Log("Hero page local level controls created under native HeroName");
    }
}

void HeroPageTier::Update()
{
    if (!LocalPlus::Enabled() || GetTickCount64() < Next) return;
    Next = GetTickCount64() + 300;
    Panorama::Scope Scope; if (!Scope.IsValid()) return;
    LoadNames();
    auto Root = Panel::Root(); auto Content = Root.FindChildTraverse("HeroPageContent"); Root.Release();
    auto Page = Content.Parent();
    if (!Content.IsValid() || Page.Type() != "DOTAHeroPageNewV2") { Page.Release(); Content.Release(); return; }
    auto Name = Content.FindChild("HeroName");
    const auto Current = Resolve(Name);
    auto Existing = Content.FindChild("OvrHeroPageTier");
    const bool New = !Existing.IsValid();
    if (New) Create(Content);
    else if (!Controls.IsValid() || Existing.Native() != Controls.Native()) { Controls.Release(); Controls = Existing; Existing = {}; }
    Existing.Release();
    const bool Changed = Current != Hero; Hero = Current;
    const auto Saved = Hero ? HeroProgress::Level(Hero) : 0;
    MenuBadge(Content, Saved);
    if (New || Changed || Saved != Observed)
    {
        Observed = Saved; Draft = Saved ? Saved : 30;
        Status = !Hero ? "Не удалось определить героя — выбор отключён" : Saved ? "Сохранён уровень " + std::to_string(Saved) : "Сейчас используется настоящий уровень Dota";
        Display();
    }
    auto Row = Controls.FindChild("OvrHeroLevelRow"); Row.SetBool("enabled", Hero != 0); Row.Release();
    auto Slider = Controls.FindChild("OvrHeroLevelSlider"); Slider.SetBool("enabled", Hero != 0); Slider.Release();
    double X = 0, Y = 0; const auto Scale = Content.Number("actualuiscale_x");
    if (Scale > 0 && Name.PositionWithin(Content, X, Y))
    {
        Controls.SetStyle("margin-left", (std::to_string(X / Scale) + "px").c_str());
        Controls.SetStyle("margin-top", (std::to_string(Y / Scale + Name.Number("actuallayoutheight") / Scale) + "px").c_str());
    }
    auto Left = Content.FindChild("LeftColumn"); Left.SetStyle("padding-top", "160px"); Left.Release();
    Name.Release(); Page.Release(); Content.Release();
}
