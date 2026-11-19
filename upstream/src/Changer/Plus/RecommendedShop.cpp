#include "RecommendedShop.h"
#include "ProTrackerData.h"
#include <cstdio>
namespace
{
    void Label(const Panel& Parent, const char* Id, const std::string& Text, const char* Size = "12px", const char* Color = "#d0d9dc")
    {
        auto P = Panel::Create("Label", Parent, Id); P.SetText(Text.c_str()); P.SetBool("hittest", false);
        P.SetStyle({{"font-family", "Radiance"}, {"font-size", Size}, {"color", Color}, {"white-space", "normal"}, {"width", "100%"}, {"margin-bottom", "3px"}}); P.Release();
    }
    std::string Percent(double P) { if (P < 0) return "—"; char B[32]; std::snprintf(B, sizeof(B), "%.0f%%", P); return B; }
    std::string Localize(const std::string& Name)
    {
        const auto Token = "#DOTA_Tooltip_ability_" + Name; V8::Local A[] = {Panorama::String(Token.c_str())};
        auto V = Panorama::Invoke(Panorama::Dollar(), "Localize", 1, A);
        const auto Text = V ? V8::ToString(Panorama::Isolate(), V) : Token; return Text == Token ? Name : Text;
    }
    void Role(std::intptr_t V) { ProTrackerData::SetRole(static_cast<unsigned>(V) >> 3, static_cast<unsigned>(V) & 7); }
    void NativeChildren(const Panel& Host, bool Replace)
    {
        auto Children = Host.Children();
        for (auto& P : Children)
        {
            if (P.Id() != "OvrD2ptBuilds")
            {
                auto Saved = V8::Get(Panorama::Isolate(), P.Handle(), Panorama::Context(), "__ovrD2ptVisibility");
                const bool Has = Saved && V8::TypeOf(Panorama::Isolate(), Saved) == "string";
                if (Replace)
                {
                    if (!Has)
                    {
                        auto S = V8::Get(Panorama::Isolate(), P.Handle(), Panorama::Context(), "style");
                        auto V = S ? V8::Get(Panorama::Isolate(), S, Panorama::Context(), "visibility") : nullptr;
                        const auto Text = V ? V8::ToString(Panorama::Isolate(), V) : "";
                        V8::Set(P.Handle(), Panorama::Context(), Panorama::String("__ovrD2ptVisibility"), Panorama::String(Text.c_str()));
                    }
                    P.SetVisible(false);
                }
                else if (Has)
                {
                    const auto Text = V8::ToString(Panorama::Isolate(), Saved);
                    P.SetStyle("visibility", Text.empty() ? "visible" : Text.c_str()); P.SetBool("__ovrD2ptVisibility", false);
                }
            }
            P.Release();
        }
    }
    void Render(const Panel& Own, unsigned Hero, unsigned Selected, const ProTrackerData::Snapshot& Data)
    {
        Label(Own, "Source", Data.Source.empty() ? "DotaCoach" : Data.Source, "17px", "#d8bd82");
        auto Roles = Panel::Create("Panel", Own, "Roles"); Roles.SetStyle({{"width", "100%"}, {"flow-children", "right-wrap"}});
        const char* Names[] = {"Керри", "Мид", "Офф", "4", "5"};
        for (unsigned I = 1; I <= 5; ++I)
        {
            auto B = Panel::Create("Button", Roles, ("Role" + std::to_string(I)).c_str());
            B.SetStyle({{"width", "19%"}, {"height", "25px"}, {"margin-right", "1%"}, {"background-color", I == Selected ? "#75613c" : "#29383f"}, {"padding", "4px 2px"}});
            B.SetBool("hittest", true); B.SetBool("hittestchildren", false); Label(B, "Text", Names[I - 1], "11px");
            B.SetEvent("onactivate", Role, (Hero << 3) | I); B.Release();
        }
        Roles.Release(); Label(Own, "Status", Data.Status.empty() ? "Ожидание данных выбранной позиции…" : Data.Status, "11px");
        if (Selected >= 4) Label(Own, "SharedRole", "Общий гайд поддержки для позиций 4/5", "10px", "#a4b4bc");
        if (Data.Busy) { auto S = Panel::Create("Panel", Own, "Loading"); S.AddClass("Spinner"); S.SetStyle({{"width", "20px"}, {"height", "20px"}}); S.Release(); }
        if (!Data.HasBuild) return;
        if (Data.Statistical) Label(Own, "Sample", "WR " + Percent(Data.WinRate) + " · проценты — частота покупки", "10px", "#a4b4bc");
        else Label(Own, "Sample", "Рекомендации автора гайда", "10px", "#a4b4bc");
        if (!Data.Updated.empty()) Label(Own, "Updated", "Данные источника: " + Data.Updated.substr(0, 48), "10px");
        const char* Phases[] = {"Стартовые предметы", "Ранняя игра", "Середина игры", "Поздняя игра"};
        for (unsigned Phase = 0; Phase < 4; ++Phase)
        {
            Label(Own, ("Title" + std::to_string(Phase)).c_str(), Phases[Phase], "14px", "#d8bd82");
            auto Row = Panel::Create("Panel", Own, ("Phase" + std::to_string(Phase)).c_str()); Row.SetStyle({{"width", "100%"}, {"flow-children", "right-wrap"}, {"margin-bottom", "7px"}});
            unsigned Index = 0;
            for (const auto& I : Data.Stages[Phase])
            {
                auto Box = Panel::Create("Panel", Row, ("Item" + std::to_string(Index++)).c_str()); Box.SetStyle({{"width", "24%"}, {"flow-children", "down"}, {"margin-right", "1%"}});
                auto Item = Panel::CreateWithProperties("DOTAShopItem", Box, "Item", {{"abilityid", std::to_string(I.Id)}, {"draggable", "true"}});
                Item.SetStyle({{"width", "100%"}, {"height", "34px"}}); Item.Release();
                if (I.Pick >= 0) Label(Box, "Rate", Percent(I.Pick), "10px", I.Pick >= 50 ? "#a2d1a5" : "#bdc7ca"); Box.Release();
            }
            Row.Release();
        }
        Label(Own, "SkillTitle", "Прокачка способностей", "14px", "#d8bd82");
        auto Skills = Panel::Create("Panel", Own, "Skills"); Skills.SetStyle({{"width", "100%"}, {"flow-children", "right-wrap"}});
        for (const auto& S : Data.Skills)
        {
            auto Box = Panel::Create("Panel", Skills, ("Skill" + std::to_string(S.Level)).c_str()); Box.SetStyle({{"width", "19%"}, {"flow-children", "down"}, {"margin-right", "1%"}});
            auto Icon = Panel::CreateWithProperties("DOTAAbilityImage", Box, "Icon", {{"abilityname", S.Name}}); Icon.SetStyle({{"width", "100%"}, {"height", "35px"}}); Icon.Release();
            Label(Box, "Level", std::to_string(S.Level), "10px"); Box.Release();
        }
        Skills.Release(); Label(Own, "TalentTitle", "Дерево талантов", "14px", "#d8bd82");
        for (const auto& T : Data.Talents)
        {
            Label(Own, ("TalentLevel" + std::to_string(T.Level)).c_str(), std::to_string(T.Level), "12px", "#d8bd82");
            auto Row = Panel::Create("Panel", Own, ("Talent" + std::to_string(T.Level)).c_str()); Row.SetStyle({{"width", "100%"}, {"flow-children", "right"}, {"margin-bottom", "5px"}});
            for (unsigned Side = 0; Side < 2; ++Side)
            {
                const auto& Name = Side ? T.Right : T.Left;
                if (Name.empty()) continue;
                auto Box = Panel::Create("Panel", Row, Side ? "Right" : "Left"); Box.SetStyle({{"width", T.Left.empty() || T.Right.empty() ? "100%" : "50%"}, {"padding", "3px"}, {"flow-children", "down"}, {"background-color", "#253139"}});
                Label(Box, "Name", Localize(Name), "10px");
                const auto Rate = Side ? T.RightPick : T.LeftPick;
                if (Rate >= 0) Label(Box, "Rate", Percent(Rate), "10px", "#cbb37d"); Box.Release();
            }
            Row.Release();
        }
    }
    void Host(const Panel& Host, bool Active, unsigned Hero)
    {
        if (!Host.IsValid()) return;
        NativeChildren(Host, Active); auto Own = Host.FindChild("OvrD2ptBuilds");
        if (!Active) { Own.SetVisible(false); Own.Release(); return; }
        const auto Role = ProTrackerData::Role(Hero); ProTrackerData::Request(Hero, Role); const auto Data = ProTrackerData::Get(Hero, Role);
        const auto Stamp = std::to_string(Hero) + ":" + std::to_string(Role) + ":" + std::to_string(Data.Revision) + ":" + Data.Status;
        auto V = Own.Handle() ? V8::Get(Panorama::Isolate(), Own.Handle(), Panorama::Context(), "__ovrD2ptStamp") : nullptr;
        if (!Own.IsValid() || !V || V8::ToString(Panorama::Isolate(), V) != Stamp)
        {
            if (Own.IsValid()) Own.Delete(); Own.Release(); Own = Panel::Create("Panel", Host, "OvrD2ptBuilds");
            Own.SetStyle({{"width", "100%"}, {"height", "fit-children"}, {"flow-children", "down"}, {"overflow", "squish scroll"}});
            Render(Own, Hero, Role, Data); V8::Set(Own.Handle(), Panorama::Context(), Panorama::String("__ovrD2ptStamp"), Panorama::String(Stamp.c_str()));
        }
        Own.SetVisible(true); Own.Release();
    }
}
bool RecommendedShop::Update(const Panel& Shop, unsigned Hero)
{
    const bool Active = ProTrackerData::Enabled() && Hero;
    auto Build = Shop.FindChildTraverse("ItemBuild"), Categories = Build.FindChildTraverse("Categories");
    auto Plus = Shop.FindChildTraverse("ItemPlusGuide"); auto Panels = Plus.FindChildrenWithClass("PlusGuideContents");
    const bool SelectedPlus = Shop.HasClass("PlusGuide");
    Host(Categories, Active && !SelectedPlus, Hero);
    for (auto& P : Panels) { Host(P, Active && SelectedPlus, Hero); P.Release(); }
    const bool Found = SelectedPlus ? !Panels.empty() : Categories.IsValid();
    Categories.Release(); Build.Release(); Plus.Release(); return Active && Found;
}
