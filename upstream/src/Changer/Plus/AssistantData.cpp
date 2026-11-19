#include "AssistantData.h"
#include "BattleData.h"
#include "MetaData.h"
#include "PlayerAnalysis.h"
#include "PlayerSignals.h"
#include "BuildData.h"

#include <windows.h>
#include <winhttp.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace
{
    using Json = google::protobuf::Value;
    using Clock = std::chrono::steady_clock;
    using Names = std::map<std::uint32_t, std::string>;
    constexpr std::size_t MaxResponseBytes = 5 * 1024 * 1024;
    constexpr auto RetryInterval = std::chrono::minutes(1);

    struct InternetHandle
    {
        HINTERNET Value = nullptr;
        explicit InternetHandle(HINTERNET Handle) : Value(Handle)
        {
            if (!Value)
                throw std::runtime_error("WinHTTP error " + std::to_string(GetLastError()));
        }
        ~InternetHandle() { WinHttpCloseHandle(Value); }
        InternetHandle(const InternetHandle&) = delete;
        InternetHandle& operator=(const InternetHandle&) = delete;
    };

    void Require(bool Condition, const char* Message)
    {
        if (!Condition)
            throw std::runtime_error(std::string(Message) + " (" + std::to_string(GetLastError()) + ")");
    }

    Json Parse(const std::string& Text)
    {
        Json Value;
        auto Status = google::protobuf::util::JsonStringToMessage(Text, &Value);
        if (!Status.ok())
            throw std::runtime_error("Invalid JSON response");
        return Value;
    }

    const Json* Field(const Json& Value, const char* Name)
    {
        if (Value.kind_case() != Json::kStructValue)
            return nullptr;
        const auto& Fields = Value.struct_value().fields();
        auto It = Fields.find(Name);
        return It == Fields.end() ? nullptr : &It->second;
    }

    const google::protobuf::ListValue& List(const Json& Value)
    {
        if (Value.kind_case() != Json::kListValue)
            throw std::runtime_error("Expected JSON array");
        return Value.list_value();
    }

    double Number(const Json& Value)
    {
        if (Value.kind_case() != Json::kNumberValue || !std::isfinite(Value.number_value()))
            throw std::runtime_error("Expected finite JSON number");
        return Value.number_value();
    }

    double Number(const Json& Value, const char* Name)
    {
        auto* Child = Field(Value, Name);
        if (!Child)
            throw std::runtime_error(std::string("Missing field ") + Name);
        return Number(*Child);
    }

    std::uint64_t Integer(const Json& Value)
    {
        double ValueNumber = Number(Value);
        if (ValueNumber < 0 || ValueNumber > 9007199254740991.0 || std::floor(ValueNumber) != ValueNumber)
            throw std::runtime_error("Invalid positive integer");
        return static_cast<std::uint64_t>(ValueNumber);
    }

    std::uint32_t Id(const std::string& Text)
    {
        if (Text.empty() || Text.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Invalid numeric ID");
        auto NumberValue = std::stoull(Text);
        if (NumberValue > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Numeric ID too large");
        return static_cast<std::uint32_t>(NumberValue);
    }

    std::string Http(const wchar_t* Host, const std::wstring& Path)
    {
        InternetHandle Session(WinHttpOpen(L"OVR-Local-Assistant/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        Require(WinHttpSetTimeouts(Session.Value, 5000, 5000, 5000, 5000), "Cannot set HTTP timeouts");
        InternetHandle Connection(WinHttpConnect(Session.Value, Host, INTERNET_DEFAULT_HTTPS_PORT, 0));
        InternetHandle Request(WinHttpOpenRequest(Connection.Value, L"GET", Path.c_str(), nullptr,
                                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        DWORD RedirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        Require(WinHttpSetOption(Request.Value, WINHTTP_OPTION_REDIRECT_POLICY, &RedirectPolicy, sizeof(RedirectPolicy)), "Cannot set redirect policy");
        Require(WinHttpSendRequest(Request.Value, L"Accept: application/json\r\n", static_cast<DWORD>(-1),
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0), "HTTP send failed");
        Require(WinHttpReceiveResponse(Request.Value, nullptr), "HTTP response failed");
        DWORD Status = 0, StatusSize = sizeof(Status);
        Require(WinHttpQueryHeaders(Request.Value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &Status, &StatusSize, WINHTTP_NO_HEADER_INDEX), "HTTP status missing");
        if (Status != 200)
            throw std::runtime_error("HTTP " + std::to_string(Status));
        std::string Response;
        for (;;)
        {
            DWORD Available = 0;
            Require(WinHttpQueryDataAvailable(Request.Value, &Available), "HTTP read failed");
            if (!Available)
                break;
            if (Available > MaxResponseBytes - Response.size())
                throw std::runtime_error("Response exceeds 5 MiB");
            auto Start = Response.size();
            Response.resize(Start + Available);
            DWORD Read = 0;
            Require(WinHttpReadData(Request.Value, Response.data() + Start, Available, &Read), "HTTP body read failed");
            Response.resize(Start + Read);
            if (!Read)
                break;
        }
        return Response;
    }

    std::filesystem::path DataDirectory()
    {
        HMODULE Module = nullptr;
        Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(&DataDirectory), &Module), "Module path unavailable");
        std::wstring Path(32768, L'\0');
        DWORD Length = GetModuleFileNameW(Module, Path.data(), static_cast<DWORD>(Path.size()));
        if (!Length || Length >= Path.size())
            throw std::runtime_error("Module path unavailable");
        Path.resize(Length);
        return std::filesystem::path(Path).parent_path() / L"assistant";
    }

    Names ReadNames(const std::filesystem::path& File)
    {
        std::ifstream Input(File, std::ios::binary);
        if (!Input)
            throw std::runtime_error("Missing assistant name map");
        Input.seekg(0, std::ios::end);
        auto Length = Input.tellg();
        if (Length < 0 || Length > static_cast<std::streamoff>(MaxResponseBytes))
            throw std::runtime_error("Invalid assistant name map size");
        Input.seekg(0);
        std::string Data(static_cast<std::size_t>(Length), '\0');
        Input.read(Data.data(), static_cast<std::streamsize>(Data.size()));
        auto Value = Parse(Data);
        if (Value.kind_case() != Json::kStructValue)
            throw std::runtime_error("Invalid assistant name map");
        Names Out;
        for (const auto& [Key, Name] : Value.struct_value().fields())
        {
            // Public constants occasionally contain malformed composite keys such
            // as "3060,1617". Ignore that entry rather than disabling every hero;
            // never invent an ID mapping from an ambiguous key.
            if (Key.empty() || Key.size() > 10 || Key.find_first_not_of("0123456789") != std::string::npos)
                continue;
            auto Numeric = std::stoull(Key);
            if (Numeric <= std::numeric_limits<std::uint32_t>::max() &&
                Name.kind_case() == Json::kStringValue && !Name.string_value().empty())
                Out.emplace(static_cast<std::uint32_t>(Numeric), Name.string_value());
        }
        if (Out.empty())
            throw std::runtime_error("Empty assistant name map");
        return Out;
    }

    std::vector<AssistantData::Item> ItemStage(const Json& Root, const char* Stage, const Names& ItemNames)
    {
        auto* Value = Field(Root, Stage);
        if (!Value || Value->kind_case() != Json::kStructValue)
            throw std::runtime_error(std::string("Missing popularity stage ") + Stage);
        std::vector<AssistantData::Item> Out;
        for (const auto& [Key, Frequency] : Value->struct_value().fields())
        {
            auto ItemId = Id(Key);
            auto Name = ItemNames.find(ItemId);
            double Count = Number(Frequency);
            if (ItemId && Name != ItemNames.end() && Count > 0)
                Out.push_back({ItemId, "item_" + Name->second, Count});
        }
        std::sort(Out.begin(), Out.end(), [](const auto& A, const auto& B) {
            return A.Frequency != B.Frequency ? A.Frequency > B.Frequency : A.Id < B.Id;
        });
        if (Out.size() > 8)
            Out.resize(8);
        return Out;
    }

    void ParseItems(const Json& Value, const Names& ItemNames, AssistantData::Snapshot& Out)
    {
        Out.StartingItems = ItemStage(Value, "start_game_items", ItemNames);
        Out.EarlyItems = ItemStage(Value, "early_game_items", ItemNames);
        Out.MidItems = ItemStage(Value, "mid_game_items", ItemNames);
        Out.LateItems = ItemStage(Value, "late_game_items", ItemNames);
        if (Out.StartingItems.empty() && Out.EarlyItems.empty() && Out.MidItems.empty() && Out.LateItems.empty())
            throw std::runtime_error("No mapped item observations");
    }

    void ParseTimed(const Json& Value, AssistantData::Snapshot& Out)
    {
        auto* Chunks = Field(Value, "rank_chunked_stats");
        if (!Chunks)
            throw std::runtime_error("No timed rank statistics");
        const Json* Chosen = nullptr;
        for (const auto& Chunk : List(*Chunks).values())
            if (auto* Rank = Field(Chunk, "rank_chunk"); Rank && Integer(*Rank) == 0)
            {
                Chosen = &Chunk;
                break;
            }
        auto* Points = Chosen ? Field(*Chosen, "timed_stats") : nullptr;
        if (!Points)
            throw std::runtime_error("No all-ranks timed statistics");
        Out.TimedStats.clear();
        for (const auto& Point : List(*Points).values())
        {
            auto* Stats = Field(Point, "all_stats");
            if (!Stats)
                continue;
            double Time = Number(Point, "time");
            double Hits = Number(*Stats, "last_hits"), Denies = Number(*Stats, "denies"), Worth = Number(*Stats, "net_worth");
            if (Time < 0 || Time > 86400 || std::floor(Time) != Time || Hits < 0 || Denies < 0 || Worth < 0)
                continue;
            Out.TimedStats.push_back({static_cast<std::uint32_t>(Time), Hits, Denies, Worth});
            if (Out.TimedStats.size() > 512)
                throw std::runtime_error("Too many timed points");
        }
        std::sort(Out.TimedStats.begin(), Out.TimedStats.end(), [](const auto& A, const auto& B) { return A.Time < B.Time; });
        if (Out.TimedStats.empty())
            throw std::runtime_error("Empty timed statistics");
    }

    std::uint64_t FirstMatch(const Json& Value)
    {
        const auto& Matches = List(Value);
        auto* MatchId = Matches.values_size() ? Field(Matches.values(0), "match_id") : nullptr;
        auto Match = MatchId ? Integer(*MatchId) : 0;
        if (!Match)
            throw std::runtime_error("No public match for this hero");
        return Match;
    }

    void ParseSkills(const Json& Value, const Names& AbilityNames, AssistantData::Snapshot& Out)
    {
        auto* Players = Field(Value, "players");
        if (!Players)
            throw std::runtime_error("Public match is not parsed");
        for (const auto& Player : List(*Players).values())
        {
            auto* Hero = Field(Player, "hero_id");
            if (!Hero || Integer(*Hero) != Out.HeroId)
                continue;
            auto* Upgrades = Field(Player, "ability_upgrades_arr");
            if (!Upgrades || Upgrades->kind_case() != Json::kListValue || !Upgrades->list_value().values_size())
                throw std::runtime_error("No parsed ability sequence in the latest public match");
            if (Upgrades->list_value().values_size() > 60)
                throw std::runtime_error("Invalid ability sequence length");
            std::vector<std::string> Order;
            for (const auto& Upgrade : Upgrades->list_value().values())
            {
                auto AbilityId = Integer(Upgrade);
                if (AbilityId > std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error("Invalid ability ID");
                auto Name = AbilityNames.find(static_cast<std::uint32_t>(AbilityId));
                if (Name == AbilityNames.end())
                    throw std::runtime_error("Unknown ability ID; no sequence was guessed");
                Order.push_back(Name->second);
            }
            Out.SkillOrder = std::move(Order);
            return;
        }
        throw std::runtime_error("Requested hero absent from public match");
    }

    struct State
    {
        std::mutex Mutex;
        std::condition_variable Wake;
        AssistantData::Snapshot View;
        std::uint32_t Hero = 0;
        std::uint64_t Generation = 0;
        bool Pending = false;
        bool Started = false;
        std::unordered_map<std::uint32_t, Clock::time_point> Attempts;
        std::unordered_map<std::uint32_t, AssistantData::Snapshot> Cache;
    };

    State& Shared()
    {
        // The project does not support unloading the hooked DLL. Deliberately keep
        // worker state alive through process exit; never join under the loader lock.
        static State* Value = new State;
        return *Value;
    }

    bool Current(State& State, std::uint64_t Generation)
    {
        std::lock_guard<std::mutex> Lock(State.Mutex);
        return State.Generation == Generation;
    }

    void Publish(State& State, std::uint64_t Generation, const AssistantData::Snapshot& Snapshot)
    {
        std::lock_guard<std::mutex> Lock(State.Mutex);
        if (State.Generation == Generation)
            State.View = Snapshot;
    }

    void Worker()
    {
        auto& State = Shared();
        for (;;)
        {
            std::uint32_t Hero;
            std::uint64_t Generation;
            {
                std::unique_lock<std::mutex> Lock(State.Mutex);
                State.Wake.wait(Lock, [&] { return State.Pending; });
                Hero = State.Hero;
                Generation = State.Generation;
                State.Pending = false;
            }
            if (!Hero)
                continue;
            AssistantData::Snapshot Out;
            Out.HeroId = Hero;
            Out.Busy = true;
            Out.ItemsStatus = "Получаем популярные предметы…";
            Out.TimedStatsStatus = "Получаем средние показатели Valve…";
            Out.SkillsStatus = "Получаем пример прокачки…";
            auto HeroText = std::to_wstring(Hero);
            try
            {
                auto ItemNames = ReadNames(DataDirectory() / L"item_ids.json");
                auto Response = Parse(Http(L"api.opendota.com", L"/api/heroes/" + HeroText + L"/itemPopularity"));
                ParseItems(Response, ItemNames, Out);
                Out.ItemsStatus = "OpenDota: частота выбора, без учёта пиков";
            }
            catch (const std::exception& Error) { Out.ItemsStatus = std::string("Предметы недоступны: ") + Error.what(); }
            Publish(State, Generation, Out);
            if (!Current(State, Generation))
                continue;
            try
            {
                auto Response = Parse(Http(L"www.dota2.com", L"/webapi/IDOTA2Plus/GetPlusHeroTimedStats/v001?hero_id=" + HeroText));
                ParseTimed(Response, Out);
                Out.TimedStatsStatus = "Средние Valve по всем рангам";
            }
            catch (const std::exception& Error) { Out.TimedStats.clear(); Out.TimedStatsStatus = std::string("Сравнение недоступно: ") + Error.what(); }
            Publish(State, Generation, Out);
            if (!Current(State, Generation))
                continue;
            try
            {
                auto AbilityNames = ReadNames(DataDirectory() / L"ability_ids.json");
                Out.SourceMatchId = FirstMatch(Parse(Http(L"api.opendota.com", L"/api/heroes/" + HeroText + L"/matches")));
                if (!Current(State, Generation))
                    continue;
                auto Match = Parse(Http(L"api.opendota.com", L"/api/matches/" + std::to_wstring(Out.SourceMatchId)));
                ParseSkills(Match, AbilityNames, Out);
                Out.SkillsStatus = "Пример одного матча, не универсальная сборка";
            }
            catch (const std::exception& Error) { Out.SkillOrder.clear(); Out.SkillsStatus = std::string("Прокачка недоступна: ") + Error.what(); }
            Out.Busy = false;
            {
                std::lock_guard<std::mutex> Lock(State.Mutex);
                State.Cache[Hero] = Out;
                if (State.Generation == Generation)
                    State.View = Out;
            }
        }
    }
}

void AssistantData::SetHero(std::uint32_t HeroId)
{
    if (HeroId > 65535)
        HeroId = 0;
    auto& State = Shared();
    std::lock_guard<std::mutex> Lock(State.Mutex);
    auto Now = Clock::now();
    auto Attempt = State.Attempts.find(HeroId);
    bool RecentlyAttempted = Attempt != State.Attempts.end() && Now - Attempt->second < RetryInterval;
    if (State.Hero == HeroId && (!HeroId || State.View.Busy || RecentlyAttempted))
        return;
    State.Hero = HeroId;
    ++State.Generation;
    State.Pending = false;
    State.View = Snapshot{};
    State.View.HeroId = HeroId;
    if (!HeroId)
        return;
    if (auto Cached = State.Cache.find(HeroId); Cached != State.Cache.end())
        State.View = Cached->second;
    if (RecentlyAttempted)
    {
        if (!State.Cache.count(HeroId))
        {
            State.View.ItemsStatus = "Повторный запрос доступен через минуту";
            State.View.TimedStatsStatus = State.View.ItemsStatus;
            State.View.SkillsStatus = State.View.ItemsStatus;
        }
        return;
    }
    State.Attempts[HeroId] = Now;
    State.View.Busy = true;
    State.View.ItemsStatus = "Loading public item popularity...";
    State.View.TimedStatsStatus = "Loading Valve all-ranks timed snapshot...";
    State.View.SkillsStatus = "Loading latest public match sequence...";
    State.Pending = true;
    if (!State.Started)
    {
        State.Started = true;
        std::thread(Worker).detach();
    }
    State.Wake.notify_one();
}

AssistantData::Snapshot AssistantData::Get()
{
    auto& State = Shared();
    std::lock_guard<std::mutex> Lock(State.Mutex);
    return State.View;
}

namespace
{
    std::vector<BattleData::Match> ParseHistory(const Json& Input)
    {
        std::vector<BattleData::Match> Matches;
        for (const auto& Row : List(Input).values())
        {
            BattleData::Match M;
            auto* MatchId = Field(Row, "match_id");
            if (!MatchId) throw std::runtime_error("Missing match_id");
            M.Id = Integer(*MatchId);
            auto Unsigned = [&](const char* Key, std::uint64_t Maximum) {
                auto* V = Field(Row, Key);
                if (!V) throw std::runtime_error(std::string("Missing ") + Key);
                auto N = Integer(*V);
                if (N > Maximum) throw std::runtime_error("History value out of bounds");
                return static_cast<std::uint32_t>(N);
            };
            M.Started = Unsigned("start_time", UINT32_MAX);
            M.Hero = Unsigned("hero_id", 65535);
            M.Duration = Unsigned("duration", 86400);
            auto Slot = Unsigned("player_slot", 255);
            if (!((Slot <= 4) || (Slot >= 128 && Slot <= 132)))
                throw std::runtime_error("Invalid player slot");
            auto* Win = Field(Row, "radiant_win");
            if (!Win || Win->kind_case() != Json::kBoolValue)
                continue; // Unfinished matches have no result; never invent a loss.
            M.Won = Win->bool_value() == (Slot < 128);
            auto Optional = [&](const char* Key) {
                auto* V = Field(Row, Key);
                if (!V || V->kind_case() == Json::kNullValue) return -1;
                auto N = Integer(*V);
                if (N > 1000000) throw std::runtime_error("Invalid match statistic");
                return static_cast<int>(N);
            };
            M.Kills = Optional("kills"); M.Deaths = Optional("deaths"); M.Assists = Optional("assists");
            M.Gpm = Optional("gold_per_min"); M.Xpm = Optional("xp_per_min");
            M.Mode = Optional("game_mode"); M.Lobby = Optional("lobby_type"); M.Lane = Optional("lane_role");
            if (M.Id && M.Hero) Matches.push_back(M);
        }
        std::sort(Matches.begin(), Matches.end(), [](const auto& A, const auto& B) { return A.Started > B.Started; });
        if (Matches.size() > 100) Matches.resize(100);
        return Matches;
    }
    struct HistoryState
    {
        std::mutex Mutex;
        BattleData::Snapshot View;
        Clock::time_point Attempt{};
    };
    HistoryState& History() { static auto* S = new HistoryState; return *S; }
}

void BattleData::Request(std::uint32_t Account)
{
    if (!Account) return;
    auto& S = History();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    if (S.View.Busy || (S.View.Account == Account && Clock::now() - S.Attempt < RetryInterval)) return;
    S.Attempt = Clock::now();
    S.View = Snapshot{Account, S.View.Revision + 1, true, "Загружаем мои матчи из OpenDota…", {}};
    std::thread([Account] {
        Snapshot Out;
        Out.Account = Account;
        try
        {
            Out.Matches = ParseHistory(Parse(Http(L"api.opendota.com", L"/api/players/" + std::to_wstring(Account) + L"/matches?limit=100")));
            Out.Status = Out.Matches.empty()
                ? "Матчей нет. История может быть закрыта или ещё не проиндексирована OpenDota."
                : "OpenDota · мои последние 100 матчей максимум · задержка индексации возможна";
        }
        catch (const std::exception& E) { Out.Status = std::string("История недоступна: ") + E.what() + ". Повтор — через минуту."; }
        auto& S = History();
        std::lock_guard<std::mutex> Lock(S.Mutex);
        Out.Revision = S.View.Revision + 1;
        S.View = std::move(Out);
    }).detach();
}

BattleData::Snapshot BattleData::Get()
{
    auto& S = History();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    return S.View;
}

namespace
{
    double MetaNumber(const Json& Row, const std::string& Key, double Fallback = 0)
    {
        auto* V = Field(Row, Key.c_str());
        if (!V || V->kind_case() == Json::kNullValue) return Fallback;
        if (V->kind_case() == Json::kNumberValue) return Number(*V);
        if (V->kind_case() == Json::kStringValue)
        {
            const auto& Text = V->string_value();
            if (Text.empty() || Text.find_first_not_of("0123456789.") != std::string::npos) throw std::runtime_error("Invalid numeric counter");
            std::size_t Used = 0; double N = std::stod(Text, &Used);
            if (Used == Text.size() && std::isfinite(N)) return N;
        }
        throw std::runtime_error("Invalid meta counter");
    }
    MetaData::Counts MetaCounts(double Picks, double Wins)
    {
        if (Picks < 0 || Wins < 0 || Wins > Picks || Picks > 1e12) throw std::runtime_error("Invalid wins/picks");
        return {Picks, Wins};
    }
    std::vector<MetaData::Hero> ParseMeta(const Json& Input)
    {
        std::vector<MetaData::Hero> Out;
        for (const auto& Row : List(Input).values())
        {
            MetaData::Hero H;
            const auto HeroId = MetaNumber(Row, "id");
            if (HeroId < 1 || HeroId > 65535 || std::floor(HeroId) != HeroId) continue;
            H.Id = static_cast<unsigned>(HeroId);
            const auto* Name = Field(Row, "localized_name"), *Internal = Field(Row, "name");
            if (!H.Id || H.Id > 65535 || !Name || !Internal || Name->kind_case() != Json::kStringValue || Internal->kind_case() != Json::kStringValue) continue;
            H.Name = Name->string_value(); H.Internal = Internal->string_value();
            H.Ranks[0] = MetaCounts(MetaNumber(Row, "pub_pick"), MetaNumber(Row, "pub_win"));
            for (unsigned R = 1; R <= 8; ++R) H.Ranks[R] = MetaCounts(MetaNumber(Row, std::to_string(R) + "_pick"), MetaNumber(Row, std::to_string(R) + "_win"));
            Out.push_back(std::move(H));
        }
        if (Out.empty()) throw std::runtime_error("No hero statistics in response");
        return Out;
    }
    void ParseMetaLanes(const Json& Input, std::vector<MetaData::Hero>& Heroes)
    {
        for (const auto& Row : List(Input).values())
        {
            if (MetaNumber(Row, "time") != 900) continue;
            const auto Hero = MetaNumber(Row, "hero_id"), Lane = MetaNumber(Row, "lane_role");
            if (Lane < 1 || Lane > 4 || std::floor(Lane) != Lane) continue;
            auto It = std::find_if(Heroes.begin(), Heroes.end(), [&](const auto& H) { return H.Id == Hero; });
            if (It != Heroes.end())
            {
                auto Stats = MetaCounts(MetaNumber(Row, "games"), MetaNumber(Row, "wins"));
                It->Lanes[static_cast<unsigned>(Lane)].Picks += Stats.Picks; It->Lanes[static_cast<unsigned>(Lane)].Wins += Stats.Wins;
            }
        }
    }
    struct MetaState
    {
        std::mutex Mutex;
        MetaData::Snapshot View;
        Clock::time_point Attempt{};
    };
    MetaState& MetaShared() { static auto* S = new MetaState; return *S; }
    struct DetailState
    {
        std::mutex Mutex;
        MetaData::Detail View;
        std::map<unsigned, MetaData::Detail> Cache;
        std::map<unsigned, Clock::time_point> Attempts;
    };
    DetailState& DetailShared() { static auto* S = new DetailState; return *S; }
    void AddFacetSample(const Json& Match, unsigned Hero, MetaData::Detail& Out)
    {
        const auto* Win = Field(Match, "radiant_win"), *Players = Field(Match, "players");
        if (!Win || Win->kind_case() != Json::kBoolValue || !Players || Players->kind_case() != Json::kListValue) return;
        for (const auto& Player : Players->list_value().values())
        {
            if (MetaNumber(Player, "hero_id") != Hero) continue;
            const auto Variant = MetaNumber(Player, "hero_variant");
            const auto Slot = MetaNumber(Player, "player_slot", -1);
            if (Variant < 1 || Variant > 255 || std::floor(Variant) != Variant || !((Slot >= 0 && Slot <= 4) || (Slot >= 128 && Slot <= 132))) return;
            auto It = std::find_if(Out.Facets.begin(), Out.Facets.end(), [&](const auto& F) { return F.Variant == Variant; });
            if (It == Out.Facets.end()) { Out.Facets.push_back({static_cast<unsigned>(Variant), {}}); It = Out.Facets.end() - 1; }
            ++It->Stats.Picks; if (Win->bool_value() == (Slot < 128)) ++It->Stats.Wins;
            ++Out.ParsedMatches; return;
        }
    }
}

void MetaData::Request()
{
    auto& S = MetaShared(); std::lock_guard<std::mutex> Lock(S.Mutex);
    if (S.View.Busy || (S.Attempt != Clock::time_point{} && Clock::now() - S.Attempt < std::chrono::minutes(15))) return;
    S.Attempt = Clock::now(); S.View.Busy = true; S.View.Status = "Загружаем статистику OpenDota…"; ++S.View.Revision;
    std::thread([] {
        auto& S = MetaShared(); MetaData::Snapshot Out;
        { std::lock_guard<std::mutex> Lock(S.Mutex); Out = S.View; }
        try
        {
            Out.Heroes = ParseMeta(Parse(Http(L"api.opendota.com", L"/api/heroStats")));
            Out.Updated = static_cast<std::uint64_t>(std::time(nullptr));
            Out.Status = "OpenDota: публичные матчи. Обновление не чаще 15 минут. Пикрейт — оценка для матчей с 10 героями.";
            try { ParseMetaLanes(Parse(Http(L"api.opendota.com", L"/api/scenarios/laneRoles?time=900")), Out.Heroes); Out.LaneStatus = "Линии: отдельный срез разобранных матчей OpenDota, time=900, без рангов."; }
            catch (const std::exception& E) { Out.LaneStatus = std::string("Линии недоступны: ") + E.what(); }
        }
        catch (const std::exception& E) { Out.Status = std::string("Мета недоступна: ") + E.what(); }
        Out.Busy = false;
        std::lock_guard<std::mutex> Lock(S.Mutex); Out.Revision = S.View.Revision + 1; S.View = std::move(Out);
    }).detach();
}
MetaData::Snapshot MetaData::Get() { auto& S = MetaShared(); std::lock_guard<std::mutex> Lock(S.Mutex); return S.View; }

double MetaData::PickRate(const std::vector<Hero>& Heroes, const Hero& H, unsigned Rank)
{
    if (Rank > 8) return -1;
    double Total = 0; for (const auto& Candidate : Heroes) Total += Candidate.Ranks[Rank].Picks;
    return Total > 0 ? H.Ranks[Rank].Picks * 1000 / Total : -1;
}
std::vector<MetaData::Hero> MetaData::Sorted(const Snapshot& S, unsigned Rank, unsigned Lane, unsigned Sort, bool Ascending)
{
    if (Rank > 8 || Lane > 4) return {};
    auto Out = S.Heroes;
    std::erase_if(Out, [&](const auto& H) { return (Lane ? H.Lanes[Lane] : H.Ranks[Rank]).Picks <= 0; });
    std::sort(Out.begin(), Out.end(), [&](const auto& A, const auto& B) {
        if (Sort == 3) return Ascending ? A.Name < B.Name : A.Name > B.Name;
        const auto& AC = Lane ? A.Lanes[Lane] : A.Ranks[Rank]; const auto& BC = Lane ? B.Lanes[Lane] : B.Ranks[Rank];
        const auto AV = Sort == 0 ? AC.WinRate() : Sort == 1 ? PickRate(S.Heroes, A, Rank) : AC.Picks;
        const auto BV = Sort == 0 ? BC.WinRate() : Sort == 1 ? PickRate(S.Heroes, B, Rank) : BC.Picks;
        return AV == BV ? A.Id < B.Id : Ascending ? AV < BV : AV > BV;
    });
    return Out;
}
void MetaData::RequestDetail(unsigned Hero)
{
    if (!Hero || Hero > 65535) return;
    auto& S = DetailShared(); std::lock_guard<std::mutex> Lock(S.Mutex);
    if (S.View.Busy) return;
    if (S.Attempts.count(Hero) && Clock::now() - S.Attempts[Hero] < std::chrono::minutes(15))
    {
        if (S.View.HeroId != Hero && S.Cache.count(Hero)) { auto Revision = S.View.Revision + 1; S.View = S.Cache[Hero]; S.View.Revision = Revision; }
        return;
    }
    S.Attempts[Hero] = Clock::now(); S.View = Detail{Hero, S.View.Revision + 1, true, "Загружаем матчапы и выборку аспектов…", {}, {}, 0};
    std::thread([Hero] {
        Detail Out; Out.HeroId = Hero;
        try
        {
            auto Rows = Parse(Http(L"api.opendota.com", L"/api/heroes/" + std::to_wstring(Hero) + L"/matchups"));
            for (const auto& R : List(Rows).values())
            {
                auto Id = MetaNumber(R, "hero_id");
                if (Id > 0 && Id <= 65535 && std::floor(Id) == Id) Out.Matchups.push_back({static_cast<unsigned>(Id), MetaCounts(MetaNumber(R, "games_played"), MetaNumber(R, "wins"))});
            }
            auto Matches = Parse(Http(L"api.opendota.com", L"/api/heroes/" + std::to_wstring(Hero) + L"/matches"));
            unsigned Count = 0;
            for (const auto& R : List(Matches).values())
            {
                if (Count++ >= 4) break;
                const auto* MatchValue = Field(R, "match_id");
                const auto MatchId = MatchValue ? Integer(*MatchValue) : 0;
                if (!MatchId) continue;
                try { AddFacetSample(Parse(Http(L"api.opendota.com", L"/api/matches/" + std::to_wstring(MatchId))), Hero, Out); }
                catch (const std::exception&) { /* Keep partial sample; never fabricate facets. */ }
            }
            Out.Status = "Аспекты: выборка до 4 доступных матчей, не общая мета. Номер аспекта — hero_variant из ответа OpenDota.";
        }
        catch (const std::exception& E) { Out.Status = std::string("Подробности недоступны: ") + E.what(); }
        auto& S = DetailShared(); std::lock_guard<std::mutex> Lock(S.Mutex);
        Out.Revision = S.View.Revision + 1; S.View = Out; S.Cache[Hero] = std::move(Out);
    }).detach();
}
MetaData::Detail MetaData::GetDetail() { auto& S = DetailShared(); std::lock_guard<std::mutex> Lock(S.Mutex); return S.View; }

namespace
{
    struct AnalysisState
    {
        std::mutex Mutex;
        std::condition_variable Wake;
        std::deque<unsigned> Pending;
        unsigned Selected = 0;
        bool Started = false;
        std::uint64_t Revision = 0;
        std::map<unsigned, Clock::time_point> Attempts;
        std::map<unsigned, PlayerAnalysis::Snapshot> Cache;
    };
    AnalysisState& AnalysisShared() { static auto* S = new AnalysisState; return *S; }
    PlayerAnalysis::Snapshot ParsePlayerAnalysis(const Json& Input, unsigned Account)
    {
        PlayerAnalysis::Snapshot Out; Out.Account = Account; Out.Matches = ParseHistory(Input);
        for (const auto& M : Out.Matches) if (M.Won) ++Out.Wins;
        for (const auto& Row : List(Input).values())
        {
            const auto* Win = Field(Row, "radiant_win");
            if (!Win || Win->kind_case() != Json::kBoolValue) continue;
            const auto Lane = MetaNumber(Row, "lane_role");
            if (Lane >= 1 && Lane <= 4 && std::floor(Lane) == Lane) ++Out.Lanes[static_cast<unsigned>(Lane)];
        }
        unsigned Total = 0; for (auto N : Out.Lanes) Total += N;
        auto Best = std::max_element(Out.Lanes.begin() + 1, Out.Lanes.end());
        const char* Names[] = {"", "безопасная линия", "центр", "сложная линия", "лес / перемещения"};
        Out.Role = Total ? std::string("Исторически чаще: ") + Names[Best - Out.Lanes.begin()] + " (" + std::to_string(*Best) + "/" + std::to_string(Total) + "). Уверенность: " + (Total >= 20 && *Best * 100 / Total >= 60 ? "средняя" : "низкая") + ". Позиции 4/5 по этим данным не различаются."
                         : "Роль не определена: в публичной истории нет lane_role.";
        Out.Status = Out.Matches.empty() ? "Публичная история пуста или недоступна OpenDota. Приватные данные не запрашиваются."
                                        : "До 100 последних публичных матчей OpenDota; это выборка, не весь аккаунт.";
        PlayerSignals::Analyze(Out);
        return Out;
    }
    std::vector<PlayerAnalysis::HeroUsage> ParsePlayerHeroes(const Json& Input)
    {
        std::vector<PlayerAnalysis::HeroUsage> Out;
        for (const auto& R : List(Input).values())
        {
            auto Id = MetaNumber(R, "hero_id"), Games = MetaNumber(R, "games", -1), Wins = MetaNumber(R, "win", -1), Time = MetaNumber(R, "last_played");
            if (Id < 1 || Id > 65535 || std::floor(Id) != Id || Games == 0) continue;
            if (Games < 0 || Games > 10000000 || Wins < 0 || Wins > Games || std::floor(Games) != Games || std::floor(Wins) != Wins || Time < 0 || Time > UINT32_MAX || std::floor(Time) != Time) throw std::runtime_error("Invalid public hero totals");
            Out.push_back({static_cast<unsigned>(Id), static_cast<unsigned>(Games), static_cast<unsigned>(Wins), static_cast<std::uint32_t>(Time), Games * ((Wins + 3.) / (Games + 6.))});
        }
        std::sort(Out.begin(), Out.end(), [](const auto& A, const auto& B) { return A.Games == B.Games ? A.Hero < B.Hero : A.Games > B.Games; });
        return Out;
    }
    void AnalysisWorker()
    {
        auto& S = AnalysisShared();
        for (;;)
        {
            unsigned Account; PlayerAnalysis::Snapshot Previous;
            {
                std::unique_lock<std::mutex> Lock(S.Mutex); S.Wake.wait(Lock, [&] { return !S.Pending.empty(); });
                Account = S.Pending.front(); S.Pending.pop_front(); Previous = S.Cache[Account];
            }
            PlayerAnalysis::Snapshot Out; Out.Account = Account;
            try
            {
                Out = ParsePlayerAnalysis(Parse(Http(L"api.opendota.com", L"/api/players/" + std::to_wstring(Account) + L"/matches?limit=100&project=match_id&project=start_time&project=duration&project=hero_id&project=player_slot&project=radiant_win&project=kills&project=deaths&project=assists&project=gold_per_min&project=xp_per_min&project=lane_role&project=game_mode&project=lobby_type")), Account);
            }
            catch (const std::exception& E) { Out.Status = std::string("Матчи недоступны: ") + E.what(); }
            try
            {
                Out.Frequent = ParsePlayerHeroes(Parse(Http(L"api.opendota.com", L"/api/players/" + std::to_wstring(Account) + L"/heroes")));
                Out.HeroSource = "Публичные агрегаты OpenDota; период может отличаться от последних 100 матчей.";
            }
            catch (const std::exception& E) { Out.HeroSource = std::string("Агрегаты недоступны: ") + E.what(); }
            if (Out.Frequent.empty() && !Out.RecentHeroes.empty()) { Out.Frequent = Out.RecentHeroes; Out.HeroSource += " Пики рассчитаны по последним 30 доступным матчам."; }
            Out.HistoricalOnly = Out.Matches.empty() && !Out.Frequent.empty();
            if (Out.HistoricalOnly) Out.Status += " Есть только публичные агрегаты. Свежая закрытая история не получена.";
            if (Out.Matches.empty() && Out.Frequent.empty() && (!Previous.Matches.empty() || !Previous.Frequent.empty()))
            {
                Out = Previous; Out.Cached = true;
                Out.Status = "Новых публичных данных нет. Показан ранее полученный публичный кэш с указанным временем загрузки.";
            }
            else Out.FetchedAt = static_cast<std::uint64_t>(std::time(nullptr));
            Out.Busy = false;
            std::lock_guard<std::mutex> Lock(S.Mutex); Out.Revision = ++S.Revision; S.Cache[Account] = std::move(Out);
        }
    }
}
void PlayerAnalysis::Request(unsigned Account, bool Select)
{
    if (!Account) return;
    auto& S = AnalysisShared(); std::lock_guard<std::mutex> Lock(S.Mutex);
    if (Select) S.Selected = Account;
    auto& View = S.Cache[Account];
    if (View.Busy || (S.Attempts.count(Account) && Clock::now() - S.Attempts[Account] < std::chrono::minutes(5))) return;
    if (S.Pending.size() >= 10) return;
    S.Attempts[Account] = Clock::now(); View.Account = Account; View.Revision = ++S.Revision; View.Busy = true; View.Status = "Загружаем публичную статистику игрока…";
    if (Select) S.Pending.push_front(Account); else S.Pending.push_back(Account);
    if (!S.Started) { S.Started = true; std::thread(AnalysisWorker).detach(); }
    S.Wake.notify_one();
}
PlayerAnalysis::Snapshot PlayerAnalysis::Get() { auto& S = AnalysisShared(); std::lock_guard<std::mutex> Lock(S.Mutex); auto It = S.Cache.find(S.Selected); return It == S.Cache.end() ? Snapshot{} : It->second; }
PlayerAnalysis::Snapshot PlayerAnalysis::Get(unsigned Account) { auto& S = AnalysisShared(); std::lock_guard<std::mutex> Lock(S.Mutex); auto It = S.Cache.find(Account); return It == S.Cache.end() ? Snapshot{} : It->second; }

namespace
{
    struct BuildState
    {
        std::mutex Mutex;
        std::map<unsigned, AssistantData::Snapshot> Cache;
        std::map<unsigned, Clock::time_point> Attempts;
        unsigned Active = 0;
    };
    BuildState& Builds() { static auto* S = new BuildState; return *S; }
}
void BuildData::Request(unsigned Hero)
{
    if (!Hero || Hero > 65535) return;
    auto& S = Builds(); std::lock_guard<std::mutex> Lock(S.Mutex);
    if (S.Active >= 2 || S.Cache[Hero].Busy || (S.Attempts.count(Hero) && Clock::now() - S.Attempts[Hero] < std::chrono::minutes(5))) return;
    S.Attempts[Hero] = Clock::now(); ++S.Active; S.Cache[Hero].HeroId = Hero; S.Cache[Hero].Busy = true; S.Cache[Hero].ItemsStatus = "Загружаем популярные предметы…";
    std::thread([Hero] {
        AssistantData::Snapshot Out; Out.HeroId = Hero;
        try { ParseItems(Parse(Http(L"api.opendota.com", L"/api/heroes/" + std::to_wstring(Hero) + L"/itemPopularity")), ReadNames(DataDirectory() / L"item_ids.json"), Out); Out.ItemsStatus = "OpenDota: число покупок, не вероятность победы и не контекстная сборка Valve."; }
        catch (const std::exception& E) { Out.ItemsStatus = std::string("Предметы недоступны: ") + E.what(); }
        auto& S = Builds(); std::lock_guard<std::mutex> Lock(S.Mutex); S.Cache[Hero] = std::move(Out); --S.Active;
    }).detach();
}
AssistantData::Snapshot BuildData::Get(unsigned Hero) { auto& S = Builds(); std::lock_guard<std::mutex> Lock(S.Mutex); auto It = S.Cache.find(Hero); return It == S.Cache.end() ? AssistantData::Snapshot{} : It->second; }
