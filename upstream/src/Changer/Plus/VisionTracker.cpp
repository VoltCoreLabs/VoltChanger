#include "VisionTracker.h"
#include "VisibilityBits.h"
#include "LocalPlus.h"
#include "SDK/Panorama/Panorama.h"
#include "SDK/SchemaSystem/SchemaOffset.h"
#include "SDK/EntitySystem/CGameEntitySystem.h"
#include "SDK/EntitySystem/EntityUtils/C_BaseEntity.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include <windows.h>
#include <map>
#include <algorithm>
#include <cmath>

namespace
{
    VisionTracker::Snapshot View;
    std::map<unsigned, VisionTracker::Observation> Seen;
    ULONGLONG Next = 0;
    double PreviousTime = -1;
    std::uintptr_t SessionHero = 0;
    CHandle DataHandle;
    int DataTeam = -1;
    VisibilityBits::State VisibleBits{};
    std::uintptr_t CurrentVisibility = 0;
    template<class T> bool Read(std::uintptr_t P, T& Out)
    {
        SIZE_T N = 0; return P && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(P), &Out, sizeof(T), &N) && N == sizeof(T);
    }
    bool SupportedClient()
    {
        static const bool Supported = [] {
            const auto Base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
            IMAGE_DOS_HEADER Dos{}; IMAGE_NT_HEADERS64 Nt{};
            return Read(Base, Dos) && Dos.e_magic == IMAGE_DOS_SIGNATURE && Dos.e_lfanew > 0 && Dos.e_lfanew < 0x10000
                && Read(Base + Dos.e_lfanew, Nt) && Nt.Signature == IMAGE_NT_SIGNATURE
                && Nt.FileHeader.TimeDateStamp == 0x6af6615f && Nt.OptionalHeader.SizeOfImage == 0x6e3b000;
        }();
        return Supported;
    }
    V8::Local Object(const char* Name) { return V8::Get(Panorama::Isolate(), Panorama::Global(), Panorama::Context(), Name); }
    double Number(V8::Local V, double Fallback = -1) { return V && V8::TypeOf(Panorama::Isolate(), V) == "number" ? V8::NumberValue(V, Panorama::Context()) : Fallback; }
    bool Boolean(const char* Method, int Entity)
    {
        V8::Local A[] = {Panorama::Number(Entity)}; auto V = Panorama::Invoke(Object("Entities"), Method, 1, A); return V && V8::BooleanValue(Panorama::Isolate(), V);
    }
    bool TeamVisibility(CGameEntitySystem* Entities, int Team)
    {
        CurrentVisibility = 0;
        auto* S = SchemaOffset::Instance();
        if (!SupportedClient() || !S->HasField("C_DOTA_DataNonSpectator", "m_bNPCVisibleState")) return false;
        const char* Name = Team == 2 ? "dota_data_radiant" : "dota_data_dire";
        auto* Data = DataTeam == Team ? Entities->Entity(DataHandle) : nullptr;
        if (!Data || !Data->DesignerName() || std::string(Data->DesignerName()) != Name)
        {
            Data = nullptr;
            for (int I = 0; I < CGameEntitySystem::MaxEntities; ++I)
            {
                auto* Candidate = Entities->Entity(I);
                if (!Candidate || !Candidate->DesignerName() || std::string(Candidate->DesignerName()) != Name) continue;
                if (Data) return false; // Ambiguous owner-data is not a valid source.
                Data = Candidate;
            }
            if (!Data) return false;
            DataHandle = Data->Handle(); DataTeam = Team;
        }
        const auto Address = reinterpret_cast<std::uintptr_t>(Data) + S->GetOffset("C_DOTA_DataNonSpectator", "m_bNPCVisibleState");
        if (!Read(Address, VisibleBits)) return false;
        CurrentVisibility = Address; return true;
    }
    bool Visible(C_BaseEntity* Entity)
    {
        auto* S = SchemaOffset::Instance();
        if (!S->HasField("C_BaseEntity", "m_pGameSceneNode") || !S->HasField("CGameSceneNode", "m_bDormant")) return false;
        const auto Index = Entity->Handle().Index();
        if (!VisibilityBits::Seen(VisibleBits, Index)) return false;
        std::uint64_t CurrentWord = 0;
        if (!CurrentVisibility || !Read(CurrentVisibility + (Index / 64) * sizeof(CurrentWord), CurrentWord) || !(CurrentWord & (std::uint64_t{1} << (Index % 64)))) return false;
        std::uintptr_t Node = 0; bool Dormant = true;
        const auto P = reinterpret_cast<std::uintptr_t>(Entity);
        return Read(P + S->GetOffset("C_BaseEntity", "m_pGameSceneNode"), Node)
            && Read(Node + S->GetOffset("CGameSceneNode", "m_bDormant"), Dormant)
            && !Dormant;
    }
    double Distance(const VisionTracker::Point& A, const VisionTracker::Point& B) { return std::hypot(A.X - B.X, A.Y - B.Y); }
}
void VisionTracker::Update()
{
    if (!LocalPlus::Enabled() || GetTickCount64() < Next) return;
    Next = GetTickCount64() + 1000;
    auto* Hero = C_DOTAPlayerController::LocalHero(); auto* Entities = CGameEntitySystem::Get();
    if (!Hero || !Entities) { Seen.clear(); View = {}; PreviousTime = -1; return; }
    Panorama::Scope Scope(Panorama::RootKind::Hud); if (!Scope.IsValid()) return;
    V8::Local A[] = {Panorama::Number(Hero->PlayerID())};
    const int Team = static_cast<int>(Number(Panorama::Invoke(Object("Players"), "GetTeam", 1, A)));
    V8::Local TimeArgs[] = {Panorama::Number(0), Panorama::Number(0)};
    const auto Time = Number(Panorama::Invoke(Object("Game"), "GetDOTATime", 2, TimeArgs), NAN);
    if (Team < 2 || Team > 3 || !std::isfinite(Time))
    {
        Seen.clear(); View.EnemyObjects.clear(); View.JungleRoute.clear();
        View.Status = "Наблюдения доступны в матче за Radiant/Dire."; return;
    }
    if (Time < PreviousTime || (SessionHero && SessionHero != reinterpret_cast<std::uintptr_t>(Hero))) Seen.clear();
    SessionHero = reinterpret_cast<std::uintptr_t>(Hero); PreviousTime = Time; View.Time = Time; View.JungleRoute.clear();
    auto* Schema = SchemaOffset::Instance();
    if (!Schema->HasField("CGameSceneNode", "m_bDormant") || !TeamVisibility(Entities, Team))
    {
        Seen.clear(); View.EnemyObjects.clear(); View.Status = "Отслеживание выключено: клиент не предоставляет проверку видимости. Скрытые позиции не читаются."; return;
    }
    struct Kind { const char* Name; const char* Label; bool Courier; bool Jungle; };
    const Kind Kinds[] = {{"npc_dota_observer_wards", "Observer", false, false}, {"npc_dota_sentry_wards", "Sentry", false, false}, {"npc_dota_courier", "Курьер", true, false}, {"npc_dota_creep_neutral", "Лагерь", false, true}};
    std::vector<VisionTracker::Point> Camps;
    for (const auto& Kind : Kinds)
    {
        V8::Local Name[] = {Panorama::String(Kind.Name)};
        auto Array = Panorama::Invoke(Object("Entities"), "GetAllEntitiesByClassname", 1, Name);
        if (Array && V8::IsObject(Array) && !V8::ArrayLength(Array)) Array = Panorama::Invoke(Object("Entities"), "GetAllEntitiesByName", 1, Name);
        if (!Array || !V8::IsObject(Array)) continue;
        for (unsigned I = 0; I < std::min(128u, V8::ArrayLength(Array)); ++I)
        {
            const int Index = static_cast<int>(Number(V8::GetIndex(Array, Panorama::Context(), I)));
            if (Index < 0 || Index >= CGameEntitySystem::MaxEntities) continue;
            auto* Entity = Entities->Entity(Index);
            if (!Entity || !Visible(Entity) || !Boolean("IsAlive", Index) || (!Kind.Jungle && !Boolean("IsEnemy", Index))) continue;
            V8::Local Args[] = {Panorama::Number(Index)};
            auto Origin = Panorama::Invoke(Object("Entities"), "GetAbsOrigin", 1, Args);
            if (!Origin || !V8::IsObject(Origin) || V8::ArrayLength(Origin) < 2) continue;
            VisionTracker::Point P{Number(V8::GetIndex(Origin, Panorama::Context(), 0), NAN), Number(V8::GetIndex(Origin, Panorama::Context(), 1), NAN), Time};
            if (!std::isfinite(P.X) || !std::isfinite(P.Y) || !Visible(Entity)) continue;
            if (Kind.Jungle)
            {
                if (std::none_of(Camps.begin(), Camps.end(), [&](const auto& C) { return Distance(C, P) < 450; })) Camps.push_back(P);
                continue;
            }
            auto Handle = Entity->Handle().Value;
            auto& O = Seen[Handle]; O.Handle = Handle; O.Kind = Kind.Label; O.Courier = Kind.Courier;
            if (O.Trail.empty() || Distance(O.Trail.back(), P) > 50 || Time - O.Trail.back().Time > 2) O.Trail.push_back(P);
            else O.Trail.back().Time = Time;
            if (O.Trail.size() > 16) O.Trail.erase(O.Trail.begin());
        }
    }
    std::erase_if(Seen, [&](const auto& Item) { return Item.second.Trail.empty() || Time - Item.second.Trail.back().Time > (Item.second.Courier ? 45 : 120); });
    View.EnemyObjects.clear(); for (const auto& [Id, O] : Seen) View.EnemyObjects.push_back(O);
    float Location[3]{};
    if (Hero->Origin(Location))
    {
        VisionTracker::Point Start{Location[0], Location[1], Time};
        while (!Camps.empty() && View.JungleRoute.size() < 5)
        {
            auto Closest = std::min_element(Camps.begin(), Camps.end(), [&](const auto& A, const auto& B) { return Distance(A, Start) < Distance(B, Start); });
            Start = *Closest; View.JungleRoute.push_back(Start); Camps.erase(Closest);
        }
    }
    View.Status = "Последние наблюдения: варды до 120с, курьер до 45с. Возраст — с последнего наблюдения, не время установки. Маршрут леса — ближайшие видимые лагеря, без автоперемещения.";
}
VisionTracker::Snapshot VisionTracker::Get() { return View; }
