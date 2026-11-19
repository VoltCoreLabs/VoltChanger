#include "MinimapOverlay.h"
#include "MinimapProjection.h"
#include "VisionTracker.h"
#include "LocalPlus.h"
#include "SDK/Panorama/Panel.h"
#include "SDK/Panorama/CUIPanel.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include "SDK/Logger/Logger.h"
#include <windows.h>
#include <cstdio>
#include <algorithm>

namespace
{
    bool Show = true, Logged = false;
    std::string Message = "Мини-карта: ожидаем загрузку карты.";
    ULONGLONG Next = 0, CalibrationUntil = 0;
    template<class T> bool Read(std::uintptr_t P, T& V) { SIZE_T N = 0; return P && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(P), &V, sizeof(V), &N) && N == sizeof(V); }
    bool Renderer(const Panel& Map, MinimapProjection::State& S)
    {
        const auto Base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
        IMAGE_DOS_HEADER Dos{}; IMAGE_NT_HEADERS64 Nt{};
        if (!Read(Base, Dos) || Dos.e_magic != IMAGE_DOS_SIGNATURE || Dos.e_lfanew < 0 || Dos.e_lfanew > 65535 || !Read(Base + Dos.e_lfanew, Nt)
            || Nt.Signature != IMAGE_NT_SIGNATURE || Nt.FileHeader.TimeDateStamp != 0x6af6615f || Nt.OptionalHeader.SizeOfImage != 0x6e3b000) return false;
        auto* Native = Map.Native(); if (!Native) return false;
        const auto Client = reinterpret_cast<std::uintptr_t>(Native->Client());
        std::uintptr_t Vtable = 0, Shared = 0, SharedVtable = 0;
        if (!Read(Client, Vtable) || Vtable != Base + 0x4ef22c8 || !Read(Client + 0x30, Shared) || !Read(Shared, SharedVtable) || SharedVtable != Base + 0x4ef22a8) return false;
        const auto D = Shared + 0x10;
        return Read(D, S.OriginX) && Read(D + 4, S.OriginY) && Read(D + 0xc, S.Scale) && Read(D + 0x10, S.Zoom)
            && Read(D + 0x9c, S.Width) && Read(D + 0xa0, S.Height) && Read(D + 0xb8, S.Left) && Read(D + 0xbc, S.Top) && Read(D + 0xc0, S.Right) && Read(D + 0xc4, S.Bottom);
    }
    std::string Pixels(double V) { char Text[32]; std::snprintf(Text, sizeof(Text), "%.2fpx", V); return Text; }
    void Dot(const Panel& Layer, const char* Id, const MinimapProjection::Point& P, const char* Color, double Size, double Opacity = 1)
    {
        auto Dot = Panel::Create("Panel", Layer, Id); Dot.SetBool("hittest", false);
        Dot.SetStyle({{"ignore-parent-flow", "true"}, {"background-color", Color}, {"border-radius", "50%"}});
        Dot.SetStyle("width", Pixels(Size).c_str()); Dot.SetStyle("height", Pixels(Size).c_str());
        Dot.SetStyle("margin-left", Pixels(P.X - Size * .5).c_str()); Dot.SetStyle("margin-top", Pixels(P.Y - Size * .5).c_str());
        char Alpha[24]; std::snprintf(Alpha, sizeof(Alpha), "%.2f", Opacity); Dot.SetStyle("opacity", Alpha); Dot.Release();
    }
}
void MinimapOverlay::Toggle(std::intptr_t) { Show = !Show; Next = 0; }
void MinimapOverlay::CheckAlignment(std::intptr_t) { Show = true; CalibrationUntil = GetTickCount64() + 15000; Next = 0; }
bool MinimapOverlay::Enabled() { return Show; }
std::string MinimapOverlay::Status() { return Message; }
void MinimapOverlay::Update()
{
    if (!LocalPlus::Enabled() || GetTickCount64() < Next) return;
    const auto Now = GetTickCount64(); Next = Now + 1000;
    Panorama::Scope Scope(Panorama::RootKind::Hud); if (!Scope.IsValid()) return;
    auto Root = Panel::Root(), Map = Root.FindChildTraverse("minimap"); Root.Release();
    if (!Map.IsValid() || Map.Type() != "DOTAMinimap") { Map.Release(); return; }
    auto Layer = Map.FindChild("OvrObservationLayer");
    if (!C_DOTAPlayerController::LocalHero()) { Layer.SetVisible(false); Layer.Release(); Map.Release(); return; }
    if (!Show) { Layer.SetVisible(false); Layer.Release(); Map.Release(); Message = "Метки мини-карты выключены."; return; }
    MinimapProjection::State State;
    const auto SX = Map.Number("actualuiscale_x"), SY = Map.Number("actualuiscale_y");
    if (!Renderer(Map, State) || SX <= 0 || SY <= 0)
    {
        Layer.SetVisible(false); Layer.Release(); Map.Release(); Message = "Метки недоступны: версия или параметры штатной мини-карты не подтверждены."; return;
    }
    const auto W = Map.Number("actuallayoutwidth") / SX, H = Map.Number("actuallayoutheight") / SY;
    if (W <= 0 || H <= 0) { Layer.Release(); Map.Release(); return; }
    if (!MinimapProjection::Project(State, State.OriginX + 512. * State.Scale, State.OriginY - 512. * State.Scale, W, H))
    {
        Layer.SetVisible(false); Layer.Release(); Map.Release(); Message = "Метки мини-карты: ожидаем корректные размеры и масштаб карты."; return;
    }
    if (!Logged)
    {
        Logged = true; CalibrationUntil = Now + 15000;
        Log("Native minimap projection: renderer=%dx%d layout=%.1fx%.1f scale=%.3f zoom=%.3f origin=%.1f %.1f insets=%d %d %d %d", State.Width, State.Height, W, H, State.Scale, State.Zoom, State.OriginX, State.OriginY, State.Left, State.Top, State.Right, State.Bottom);
    }
    if (Layer.IsValid()) Layer.Delete(); Layer.Release();
    Layer = Panel::Create("Panel", Map, "OvrObservationLayer"); Layer.SetBool("hittest", false); Layer.SetBool("hittestchildren", false);
    Layer.SetStyle({{"ignore-parent-flow", "true"}, {"width", "100%"}, {"height", "100%"}, {"z-index", "10"}, {"overflow", "clip"}});
    const auto Observations = VisionTracker::Get(); unsigned I = 0;
    for (const auto& O : Observations.EnemyObjects)
    {
        if (O.Trail.empty()) continue;
        const auto& Last = O.Trail.back(); const auto Age = std::max(0., Observations.Time - Last.Time);
        const auto Alpha = std::max(.25, 1 - Age / (O.Courier ? 45 : 120));
        if (O.Courier)
        {
            for (const auto& P : O.Trail) if (auto Projected = MinimapProjection::Project(State, P.X, P.Y, W, H)) Dot(Layer, ("CourierTrail" + std::to_string(I++)).c_str(), *Projected, "#e5b569", 3, Alpha * .7);
        }
        if (auto P = MinimapProjection::Project(State, Last.X, Last.Y, W, H)) Dot(Layer, ("Object" + std::to_string(I++)).c_str(), *P, O.Courier ? "#efbb63" : O.Kind == "Sentry" ? "#83c9e5" : "#dd8bd5", O.Courier ? 7 : 6, Alpha);
    }
    // Short own-hero reference helps verify renderer-to-layout scaling in-game.
    if (Now < CalibrationUntil) if (auto* Hero = C_DOTAPlayerController::LocalHero())
    {
        float Position[3]{};
        if (Hero->Origin(Position)) if (auto P = MinimapProjection::Project(State, Position[0], Position[1], W, H))
        {
            Dot(Layer, "OwnHeroReference", *P, "#0000", 12);
            auto Reference = Layer.FindChild("OwnHeroReference"); Reference.SetStyle("border", "2px solid #fff1ad"); Reference.Release();
            Log("Native minimap own-hero reference: world=%.1f %.1f layout=%.1f %.1f", Position[0], Position[1], P->X, P->Y);
        }
    }
    Message = "Штатная мини-карта: розовые точки — последние наблюдения Observer, голубые — Sentry, золотые — курьер и его наблюдавшийся след. Метки бледнеют со временем, положение в тумане не предсказывается.";
    Layer.Release(); Map.Release();
}
