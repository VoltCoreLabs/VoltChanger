#include "ProTrackerData.h"
#include "DotacoachParse.h"
#include "SDK/Config/Config.h"
#include <windows.h>
#include <winhttp.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <charconv>
#include <stdexcept>
#include <sstream>

namespace
{
    using Clock = std::chrono::steady_clock;
    std::filesystem::path Home()
    {
        wchar_t Path[32768]{};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", Path, 32768)) GetTempPathW(32768, Path);
        return std::filesystem::path(Path) / L"OVR";
    }
    std::filesystem::path Directory()
    {
        HMODULE M = nullptr; wchar_t Path[32768]{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Directory), &M) || !GetModuleFileNameW(M, Path, 32768)) throw std::runtime_error("Assistant directory unavailable");
        return std::filesystem::path(Path).parent_path() / L"assistant";
    }
    std::string Read(const std::filesystem::path& Path)
    {
        std::error_code E; const auto Size = std::filesystem::file_size(Path, E);
        if (E || Size > 5 * 1024 * 1024) return {};
        std::ifstream F(Path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(F), {});
    }
    ProTrackerData::Names ReadNames(const char* File)
    {
        google::protobuf::Struct O;
        if (!google::protobuf::util::JsonStringToMessage(Read(Directory() / File), &O).ok()) throw std::runtime_error("Missing assistant metadata");
        ProTrackerData::Names Out;
        for (const auto& [K, V] : O.fields())
        {
            unsigned Id = 0; auto P = std::from_chars(K.data(), K.data() + K.size(), Id);
            if (P.ec == std::errc() && P.ptr == K.data() + K.size() && V.kind_case() == google::protobuf::Value::kStringValue) Out[Id] = V.string_value();
        }
        return Out;
    }
    std::string HeroSlug(unsigned Hero)
    {
        static const auto Slugs = [] {
            std::map<unsigned, std::string> Out;
            std::istringstream Lines(Read(Directory() / "dotacoach_heroes.txt")); std::string Line;
            while (std::getline(Lines, Line))
            {
                if (Line.empty() || Line[0] == '#') continue;
                std::istringstream Row(Line); unsigned Id = 0; std::string Slug;
                if (Row >> Id >> Slug && Slug.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos) Out[Id] = Slug;
            }
            return Out;
        }();
        const auto I = Slugs.find(Hero);
        if (I == Slugs.end()) throw std::runtime_error("DotaCoach hero mapping unavailable");
        return I->second;
    }
    struct Handle
    {
        HINTERNET Value;
        explicit Handle(HINTERNET P) : Value(P) { if (!P) throw std::runtime_error("DotaCoach connection failed"); }
        ~Handle() { WinHttpCloseHandle(Value); }
    };
    struct HttpError : std::runtime_error { unsigned Status; explicit HttpError(unsigned S) : std::runtime_error("HTTP " + std::to_string(S)), Status(S) {} };
    std::string Fetch(const std::string& Slug, unsigned Role)
    {
        Handle Session(WinHttpOpen(L"OVR-Recommended-Builds/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        WinHttpSetTimeouts(Session.Value, 5000, 5000, 5000, 5000);
        Handle Connection(WinHttpConnect(Session.Value, L"dotacoach.gg", INTERNET_DEFAULT_HTTPS_PORT, 0));
        const wchar_t* Roles[] = {L"", L"carry", L"mid", L"offlane", L"support", L"support"};
        const auto Path = L"/en/heroes/" + std::wstring(Slug.begin(), Slug.end()) + L"/builds/" + Roles[Role];
        Handle Request(WinHttpOpenRequest(Connection.Value, L"GET", Path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        DWORD Redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(Request.Value, WINHTTP_OPTION_REDIRECT_POLICY, &Redirect, sizeof(Redirect));
        if (!WinHttpSendRequest(Request.Value, L"Accept: text/html\r\n", DWORD(-1), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(Request.Value, nullptr)) throw std::runtime_error("DotaCoach network unavailable");
        DWORD Status = 0, Size = sizeof(Status);
        if (!WinHttpQueryHeaders(Request.Value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &Status, &Size, WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("DotaCoach response unavailable");
        if (Status != 200) throw HttpError(Status);
        std::string Body;
        for (;;)
        {
            DWORD Available = 0, Done = 0;
            if (!WinHttpQueryDataAvailable(Request.Value, &Available)) throw std::runtime_error("DotaCoach read failed");
            if (!Available) break;
            if (Available > 5 * 1024 * 1024 - Body.size()) throw std::runtime_error("DotaCoach response too large");
            const auto Start = Body.size(); Body.resize(Start + Available);
            if (!WinHttpReadData(Request.Value, Body.data() + Start, Available, &Done)) throw std::runtime_error("DotaCoach read failed");
            Body.resize(Start + Done); if (!Done) break;
        }
        return Body;
    }
    struct Entry { ProTrackerData::Snapshot View; Clock::time_point Attempt{}; };
    struct State { std::mutex Mutex; std::map<std::pair<unsigned, unsigned>, Entry> Cache; bool Busy = false; Clock::time_point Blocked{}, Next{}; };
    State& Shared() { static auto* S = new State; return *S; }
    std::filesystem::path CachePath(unsigned H, unsigned R) { return Home() / L"cache" / L"dotacoach" / (std::to_wstring(H) + L"-" + std::to_wstring(R) + L".html"); }
}
bool ProTrackerData::Enabled()
{
    std::ifstream F(Home() / L"recommended_builds.cfg"); std::string Line;
    while (std::getline(F, Line)) if (Line == "enabled=1" || Line == "enabled=1\r") return true;
    return false;
}
void ProTrackerData::Toggle()
{
    const auto Value = !Enabled(); std::error_code E; std::filesystem::create_directories(Home(), E);
    std::ofstream F(Home() / L"recommended_builds.cfg"); if (F) F << "enabled=" << (Value ? 1 : 0) << '\n';
}
unsigned ProTrackerData::Role(unsigned Hero)
{
    const auto Text = Config::Get(("builds.role." + std::to_string(Hero)).c_str());
    return Text.size() == 1 && Text[0] >= '1' && Text[0] <= '5' ? Text[0] - '0' : 3;
}
void ProTrackerData::SetRole(unsigned Hero, unsigned Role)
{ if (Hero && Hero < 65536 && Role >= 1 && Role <= 5) Config::Set(("builds.role." + std::to_string(Hero)).c_str(), std::to_string(Role)); }
void ProTrackerData::Request(unsigned Hero, unsigned Role)
{
    if (!Enabled() || !Hero || Hero > 65535 || Role < 1 || Role > 5) return;
    auto& S = Shared(); std::lock_guard<std::mutex> Lock(S.Mutex); auto& E = S.Cache[{Hero, Role}];
    if (E.View.Busy || S.Busy || (E.Attempt != Clock::time_point{} && Clock::now() - E.Attempt < std::chrono::minutes(30))) return;
    if (Clock::now() < S.Blocked) { E.View.Status = "DotaCoach ограничил доступ (HTTP 403). Повтор позже."; return; }
    if (Clock::now() < S.Next) return;
    S.Next = Clock::now() + std::chrono::seconds(1);
    E.Attempt = Clock::now(); E.View.Busy = true; ++E.View.Revision; S.Busy = true;
    E.View.Status = "Загрузка DotaCoach…";
    std::thread([Hero, Role] {
        auto& S = Shared(); Snapshot Out;
        { std::lock_guard<std::mutex> Lock(S.Mutex); Out = S.Cache[{Hero, Role}].View; }
        bool Blocked = false;
        try
        {
            const auto Slug = HeroSlug(Hero); const auto Items = ReadNames("item_ids.json"), Abilities = ReadNames("ability_ids.json");
            const auto Path = CachePath(Hero, Role);
            if (!Out.HasBuild)
            {
                try
                {
                    const auto Raw = Read(Path);
                    if (!Raw.empty())
                    {
                        Out = DotacoachParse(Raw, Slug, Hero, Role, Items, Abilities); Out.Cached = true; Out.Busy = true;
                        Out.Status = "Сохранённая сборка DotaCoach · обновление…";
                        std::lock_guard<std::mutex> Lock(S.Mutex); Out.Revision = S.Cache[{Hero, Role}].View.Revision + 1; S.Cache[{Hero, Role}].View = Out;
                    }
                }
                catch (...) {}
            }
            const auto Raw = Fetch(Slug, Role); Out = DotacoachParse(Raw, Slug, Hero, Role, Items, Abilities);
            std::error_code E; std::filesystem::create_directories(Path.parent_path(), E);
            auto Temp = Path; Temp += L".tmp"; std::ofstream F(Temp, std::ios::binary); F << Raw; F.close();
            if (F) MoveFileExW(Temp.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
        catch (const HttpError& E)
        {
            Blocked = E.Status == 403;
            Out.Status = Blocked ? "DotaCoach: доступ ограничен (HTTP 403)." : "DotaCoach: HTTP " + std::to_string(E.Status);
            if (Out.HasBuild) { Out.Cached = true; Out.Status += " Показан сохранённый кэш."; }
        }
        catch (const std::exception& E) { Out.Status = std::string("Нет доступной сборки DotaCoach: ") + E.what(); if (Out.HasBuild) { Out.Cached = true; Out.Status += " Показан сохранённый кэш."; } }
        Out.Busy = false;
        std::lock_guard<std::mutex> Lock(S.Mutex);
        auto& E = S.Cache[{Hero, Role}]; Out.Revision = E.View.Revision + 1; E.View = std::move(Out); S.Busy = false;
        if (Blocked) S.Blocked = Clock::now() + std::chrono::minutes(30);
    }).detach();
}
ProTrackerData::Snapshot ProTrackerData::Get(unsigned Hero, unsigned Role)
{ auto& S = Shared(); std::lock_guard<std::mutex> Lock(S.Mutex); return S.Cache[{Hero, Role}].View; }
