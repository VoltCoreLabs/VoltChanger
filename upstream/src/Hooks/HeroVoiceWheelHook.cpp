#include "HeroVoiceWheelHook.h"
#include "HeroBadgeHook.h"
#include "Changer/Plus/LocalVoiceChat.h"
#include "SDK/Panorama/Panorama.h"
#include "SDK/Memory/Memory.h"
#include "SDK/Logger/Logger.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <memory>
#include <string>

namespace
{
    void (*OriginalAction)(void*, int, const std::uint32_t*) = nullptr;
    void (*OriginalPreview)(void*, unsigned) = nullptr;
    void* (*Manager)() = nullptr;
    const void* (*Message)(void*, unsigned) = nullptr;
    const unsigned* (*Required)(const void*) = nullptr;
    ULONGLONG LastPreview = 0;
    std::atomic<unsigned> SelectionReports{0};

    const void* Lookup(unsigned Id)
    {
        auto* Data = Manager(); return Data ? Message(Data, Id) : nullptr;
    }
    bool Emit(unsigned Id, const void* Entry, std::string* Played = nullptr)
    {
        const auto Now = GetTickCount64();
        if (LastPreview && Now - LastPreview < 2500) return true;
        // Actual sound-event field consumed by the native chat-wheel preview.
        const auto* Sound = Memory::Read<const char*>(reinterpret_cast<std::uintptr_t>(Entry) + 0x30);
        if (!Sound || !Sound[0]) { LogW("Hero voice local preview: message=%u has no sound", Id); return false; }
        std::size_t Length = 0;
        while (Length < 256 && Sound[Length]) ++Length;
        if (!Length || Length == 256) return false;
        const std::string Name(Sound, Length);
        auto Scope = std::make_unique<Panorama::Scope>(Panorama::RootKind::Hud);
        if (!Scope->IsValid())
        {
            Scope.reset();
            Scope = std::make_unique<Panorama::Scope>(Panorama::RootKind::Dashboard);
        }
        if (!Scope->IsValid())
        { LogW("Hero voice local preview: Panorama context unavailable message=%u", Id); return false; }
        auto Game = V8::Get(Panorama::Isolate(), Panorama::Global(), Panorama::Context(), "Game");
        auto Function = Game ? V8::Get(Panorama::Isolate(), Game, Panorama::Context(), "EmitSound") : nullptr;
        if (!Function || !V8::IsFunction(Function))
        { LogW("Hero voice local preview: Game.EmitSound unavailable message=%u", Id); return false; }
        V8::Local Args[] = {Panorama::String(Name.c_str())};
        auto Result = Panorama::Invoke(Game, "EmitSound", 1, Args);
        if (!Result || V8::TypeOf(Panorama::Isolate(), Result) != "number")
        { LogW("Hero voice local preview: no sound GUID message=%u", Id); return false; }
        LastPreview = Now;
        if (Played) *Played = Name;
        const auto Guid = V8::NumberValue(Result, Panorama::Context());
        Log("Hero voice local EmitSound: message=%u sound=%s guid=%.0f (local only)", Id, Name.c_str(), Guid);
        return true;
    }
    void Action(void* Receiver, int Kind, const std::uint32_t* Selection)
    {
        const auto Id = Selection ? Selection[0] : 0xffffffffu;
        const auto Extra = Selection ? Selection[1] : 0xffffffffu;
        if (Id == 0xffffffffu) { OriginalAction(Receiver, Kind, Selection); return; }
        const auto* Entry = Lookup(Id);
        const auto* Gate = Entry ? Required(Entry) : nullptr;
        const bool Local = Gate && HeroBadgeHook::NeedsLocalVoice(Gate[0], Gate[1]);
        if (SelectionReports.fetch_add(1) < 64)
            Log("Hero voice HUD selection: kind=%d message=%u extra=%u found=%d hero=%u required=%u local=%d", Kind, Id, Extra, Entry != nullptr, Gate ? Gate[0] : 0, Gate ? Gate[1] : 0, Local);
        if (Local)
        {
            // A locally unlocked phrase belongs only to this client. The
            // original action submits the native chat-wheel request to the
            // server (including the in-process demo server). Do not submit a
            // request whose tier exists only in our local presentation.
            std::string Played;
            if (Emit(Id, Entry, &Played) && !Played.empty()) LocalVoiceChat::Post(Id, Played);
            return;
        }
        // Naturally available phrases and unrelated wheel actions retain the
        // exact ordinary native request path.
        OriginalAction(Receiver, Kind, Selection);
    }
    void Preview(void* Receiver, unsigned Id)
    {
        const auto* Entry = Lookup(Id);
        const auto* Gate = Entry ? Required(Entry) : nullptr;
        // This callback is local preview only, including the hero page.
        const bool Local = Gate && HeroBadgeHook::AllowsLocalVoice(Gate[0], Gate[1]);
        if (SelectionReports.fetch_add(1) < 64)
            Log("Hero voice native preview selection: message=%u found=%d hero=%u required=%u local=%d", Id, Entry != nullptr, Gate ? Gate[0] : 0, Gate ? Gate[1] : 0, Local);
        if (Local && Emit(Id, Entry)) return;
        OriginalPreview(Receiver, Id);
    }
}
bool HeroVoiceWheelHook::Install()
{
    // CDOTA_Hud_ChatWheel action slot85. The old target belonged to the
    // different CDOTA_Hud_ChatWheelVsScreen and never saw match choices.
    constexpr const char* ActionPattern = "40 55 53 56 57 41 56 48 8D AC 24 20 FF FF FF 48 81 EC E0 01 00 00 49 8B F0 4C 8B F1 83 FA 03 0F 85 ? ? ? ? 48 8B 3D ? ? ? ? 48 85 FF 74 7E";
    constexpr const char* PreviewPattern = "48 89 5C 24 08 57 48 83 EC 30 8B DA 48 8B F9 E8 ? ? ? ? 8B D3 48 8B C8 E8 ? ? ? ? 45 33 C9 C7 44 24 20 FF FF FF FF 4C 8B C0 48 8D 54 24 50 48 8B CF E8 ? ? ? ? 48 8B 5C 24 40";
    constexpr const char* RequiredPattern = "48 8B 41 50 48 63 49 48 48 8D 14 49 48 8D 0C 90 48 3B C1 74 0E 83 38 04 74 0C 48 83 C0 0C 48 3B C1 75 F2 33 C0 C3 48 83 C0 04 C3";
    const auto Target = Memory::FindPattern("client.dll", ActionPattern);
    const auto PreviewTarget = Memory::FindPattern("client.dll", PreviewPattern);
    const auto Require = Memory::FindPattern("client.dll", RequiredPattern);
    if (!Target || !PreviewTarget || !Require || Memory::Read<std::uint8_t>(PreviewTarget + 0x0f) != 0xe8 || Memory::Read<std::uint8_t>(PreviewTarget + 0x19) != 0xe8)
    { LogW("Hero voice wheel/preview patterns unavailable"); return true; }
    Manager = reinterpret_cast<decltype(Manager)>(Memory::ResolveRelative(PreviewTarget + 0x0f, 1, 5));
    Message = reinterpret_cast<decltype(Message)>(Memory::ResolveRelative(PreviewTarget + 0x19, 1, 5));
    Required = reinterpret_cast<decltype(Required)>(Require);
    auto Status = MH_Initialize(); if (Status != MH_OK && Status != MH_ERROR_ALREADY_INITIALIZED) return false;
    auto* Function = reinterpret_cast<void*>(Target);
    auto* PreviewFunction = reinterpret_cast<void*>(PreviewTarget);
    Status = MH_CreateHook(Function, reinterpret_cast<void*>(Action), reinterpret_cast<void**>(&OriginalAction));
    if (Status == MH_OK) Status = MH_CreateHook(PreviewFunction, reinterpret_cast<void*>(Preview), reinterpret_cast<void**>(&OriginalPreview));
    if (Status == MH_OK) Status = MH_EnableHook(Function);
    if (Status == MH_OK) Status = MH_EnableHook(PreviewFunction);
    if (Status != MH_OK)
    {
        MH_DisableHook(Function); MH_DisableHook(PreviewFunction);
        LogE("Hero voice wheel/preview hook failed: %s", MH_StatusToString(Status)); return false;
    }
    Log("Hero voice HUD action and native preview hooks installed; local Game.EmitSound route"); return true;
}
