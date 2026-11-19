#include "LocalPlus.h"
#include "PlusState.h"
#include "Changer/Gc/GcEmulator.h"
#include "SDK/Files/Paths.h"
#include "SDK/Logger/Logger.h"
#include "SDK/SteamAPI/ISteamUser.h"
#include "SDK/Panorama/Panorama.h"
#include "SDK/Panorama/Panel.h"
#include <ctime>
#include <fstream>

bool LocalPlus::Enabled()
{
    static const bool Value = [] {
        std::ifstream File(Paths::Resolve("ovr_plus.cfg"));
        std::string Line;
        while (std::getline(File, Line))
            if (Line == "enabled=0" || Line == "enabled=0\r") return false;
        return true;
    }();
    return Value;
}
void LocalPlus::Update()
{
    static std::uint64_t SentFor = 0;
    static unsigned Frames = 0;
    if (!Enabled()) return;
    ++Frames;
    if (SentFor && Frames == 600)
    {
        Panorama::Scope Scope;
        if (Scope.IsValid())
        {
            Panel Root = Panel::Root();
            Log("Local Plus probe: dashboard PlusSubscriber=%d IsPlusSubscriber=%d", Root.HasClass("PlusSubscriber"), Root.HasClass("IsPlusSubscriber"));
            Root.Release();
        }
    }
    auto* User = ISteamUser::Get();
    if (!User || !User->GetSteamID() || User->GetSteamID() == SentFor) return;
    const auto SteamId = User->GetSteamID();
    SentFor = SteamId;
    GcEmulator::SendObject(PlusState::SoType, PlusState::LocalAccount(User->AccountId(), static_cast<std::uint32_t>(std::time(nullptr))));
    Log("Local Plus: client cache message queued; feature data availability is not yet verified");
}
bool LocalPlus::Rewrite(std::uint32_t EMsg, std::string& Body)
{
    if (!Enabled()) return false;
    auto* User = ISteamUser::Get();
    if (!User) return false;
    const bool Changed = PlusState::Rewrite(EMsg, Body, User->GetSteamID(), static_cast<std::uint32_t>(std::time(nullptr)));
    if (Changed) Log("Local Plus: updated incoming client cache message %u", EMsg);
    return Changed;
}
