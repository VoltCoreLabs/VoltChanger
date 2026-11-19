#include "NativeHistory.h"
#include "HistoryWire.h"
#include "SDK/GC/CGCClient.h"
#include "SDK/SteamAPI/ISteamUser.h"
#include "SDK/Logger/Logger.h"
#include "ovr_plus.pb.h"
#include "steammessages.pb.h"
#include <windows.h>
#include <mutex>
#include <cstring>

namespace
{
    std::mutex Mutex;
    BattleData::Snapshot View;
    ULONGLONG Attempt = 0, Deadline = 0;
    std::uint32_t Token = 0;
    bool ReportedUnrelated = false;
}

void NativeHistory::Request()
{
    auto* User = ISteamUser::Get();
    auto* Client = CGCClient::Get();
    if (!User || !User->AccountId() || !Client) return;
    auto* Gc = Client->SteamGameCoordinator();
    if (!Gc) return;
    const auto Account = User->AccountId();
    std::uint32_t RequestToken;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        auto Now = GetTickCount64();
        if (View.Busy || (View.Account == Account && Attempt && Now - Attempt < 60000)) return;
        Attempt = Now; Deadline = Now + 20000;
        Token = Token ? Token + 1 : (0x4f560000u ^ GetCurrentProcessId());
        RequestToken = Token;
        ReportedUnrelated = false;
        View = {Account, View.Revision + 1, true, "Получаем мою историю от координатора Dota…", {}};
    }
    ovr::HistoryRequest Request;
    Request.set_account_id(Account); Request.set_matches_requested(100); Request.set_request_id(RequestToken);
    CMsgProtoBufHeader Header;
    Header.set_job_id_source((static_cast<std::uint64_t>(GetCurrentProcessId()) << 32) | RequestToken);
    const auto HeaderBody = Header.SerializeAsString(), Body = Request.SerializeAsString();
    const std::uint32_t Type = 7408u | 0x80000000u, HeaderSize = static_cast<std::uint32_t>(HeaderBody.size());
    std::vector<std::uint8_t> Packet(8 + HeaderBody.size() + Body.size());
    std::memcpy(Packet.data(), &Type, 4); std::memcpy(Packet.data() + 4, &HeaderSize, 4);
    std::memcpy(Packet.data() + 8, HeaderBody.data(), HeaderBody.size());
    std::memcpy(Packet.data() + 8 + HeaderBody.size(), Body.data(), Body.size());
    // ISteamGameCoordinator::SendMessage, vtable slot 0. Uses the authenticated
    // current Steam session; no synthetic response or subscription change.
    auto Send = reinterpret_cast<int (*)(void*, std::uint32_t, const void*, std::uint32_t)>((*static_cast<void***>(Gc))[0]);
    const int Result = Send(Gc, Type, Packet.data(), static_cast<std::uint32_t>(Packet.size()));
    Log("Own match history: request token=%u send result=%d", RequestToken, Result);
    if (Result != 0)
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        if (View.Busy && Token == RequestToken) { View.Busy = false; View.Status = "Координатор Dota недоступен (" + std::to_string(Result) + ")"; ++View.Revision; }
    }
}

BattleData::Snapshot NativeHistory::Get()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (View.Busy && GetTickCount64() >= Deadline)
    {
        View.Busy = false; View.Status = "Координатор Dota не ответил за 20 секунд"; ++View.Revision;
        Log("Own match history: timed out");
    }
    return View;
}

void NativeHistory::Receive(std::uint32_t EMsg, const std::string& Body)
{
    if (EMsg != 7409) return;
    std::lock_guard<std::mutex> Lock(Mutex);
    if (!Token) return;
    if (!HistoryWire::Decode(Body, Token, View.Matches))
    {
        if (View.Busy && !ReportedUnrelated) { Log("Own match history: unrelated or malformed response ignored"); ReportedUnrelated = true; }
        return;
    }
    View.Busy = false; ++View.Revision;
    View.Status = View.Matches.empty() ? "Координатор Dota вернул пустую историю" : "Моя история от Valve · до 100 матчей · K/D/A и GPM/XPM в ответе отсутствуют";
    Log("Own match history: received %zu real matches", View.Matches.size());
}
