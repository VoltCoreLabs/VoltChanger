#include "CourierSkins.h"
#include "CourierPolicy.h"
#include "NetSkins.h"
#include "Changer/Inventory/FakeItems.h"
#include "Changer/Items/AssetPrecache.h"
#include "SDK/Econ/CEconItemSchema.h"
#include "SDK/Econ/CEconItemDefinition.h"
#include "SDK/EntitySystem/CGameEntitySystem.h"
#include "SDK/EntitySystem/EntityUtils/C_BaseEntity.h"
#include "SDK/NetDelta/ResourceId.h"
#include "SDK/ResourceSystem/CResourceSystem.h"
#include "SDK/ResourceSystem/CModel.h"
#include "SDK/Logger/Logger.h"
#include <array>
#include <mutex>
#include <unordered_map>

namespace
{
    constexpr const char* ModelField = "CBodyComponent.m_hModel";
    constexpr const char* MaterialField = "CBodyComponent.m_materialGroup";
    constexpr const char* MeshField = "CBodyComponent.m_MeshGroupMask";
    struct Model { std::uint64_t Id = 0, Material = 0; std::string Path; };
    struct State { int Player = -1; std::array<Model, 4> Models; };
    std::mutex Mutex; State Wanted;
    std::unordered_map<int, std::uint64_t> Applied;
    std::unordered_map<int, std::uint32_t> LastEdit;
    std::uint32_t Connection = 0;
    std::string LastSelection;
    void Set(const NetDelta::EntityState& Entity, const char* Field, std::uint64_t Value, NetDelta::EntityEdits& Edits)
    {
        NetDelta::FieldPath Path;
        if (!Entity.Ser->FindPath(Field, Path)) return;
        NetDelta::FieldValue V; V.U = Value; V.I = static_cast<std::int64_t>(Value);
        Edits.Fields.push_back({Path, V, false});
    }
}
bool CourierSkins::Tracked(const std::string& Name)
{
    return Name == "CDOTA_Unit_Courier" || Name == "C_DOTA_Unit_Courier";
}
void CourierSkins::Update(int LocalPlayer)
{
    State Next; Next.Player = LocalPlayer;
    auto* Schema = CEconItemSchema::Get(); auto* Resources = CResourceSystem::Get();
    CEconItemDefinition* Definition = nullptr; int Style = 0;
    if (Schema) for (const auto& Item : FakeItems::Equipped())
        for (const auto& Equipped : Item.equipped_state())
            if ((Equipped.new_class() == 1000 || Equipped.new_class() == 0) && Equipped.new_slot() == 2)
            {
                Definition = Schema->GetItemDefinition(Item.def_index());
                Style = Item.style() == 255 ? 0 : static_cast<int>(Item.style());
            }
    if (Definition)
        for (const auto& M : Definition->AssetModifiers(Style))
        {
            if ((M.Type != "courier" && M.Type != "courier_flying") || M.Modifier.empty()) continue;
            const int Base = M.Type == "courier_flying" ? 2 : 0;
            for (int Team = 0; Team < 2; ++Team)
            {
                if (!M.Asset.empty() && M.Asset != (Team ? "dire" : "radiant")) continue;
                auto& Out = Next.Models[Base + Team]; Out.Path = M.Modifier;
                const auto Id = NetDelta::ResourceId(M.Modifier.c_str());
                if (Resources && Resources->Status(Id) == CResourceSystem::EStatus::Loaded)
                {
                    Out.Id = Id; auto* Data = static_cast<CModel*>(Resources->Data(Id));
                    Out.Material = Data ? Data->MaterialGroup(Definition->StyleSkin(Style)) : 0;
                }
                else AssetPrecache::Ensure(M.Modifier);
            }
        }
    for (int Team = 0; Team < 2; ++Team)
        if (Next.Models[Team + 2].Path.empty()) Next.Models[Team + 2] = Next.Models[Team];
    const auto Selection = std::to_string(Definition ? Definition->DefIndex() : 0) + "/" + std::to_string(Style);
    if (Selection != LastSelection) { Log("Courier local selection: def/style %s player=%d", Selection.c_str(), LocalPlayer); LastSelection = Selection; }
    std::lock_guard<std::mutex> Lock(Mutex); Wanted = std::move(Next);
}
void CourierSkins::Edit(const NetDelta::PacketView& View, std::map<int, NetDelta::EntityEdits>& Edits)
{
    State Snapshot; { std::lock_guard<std::mutex> Lock(Mutex); Snapshot = Wanted; }
    auto& World = NetDelta::EntityWorld::Instance();
    if (Connection != World.Connection()) { Connection = World.Connection(); Applied.clear(); LastEdit.clear(); }
    for (const auto& [Index, Flag] : View.CreatedSet) { Applied.erase(Index); LastEdit.erase(Index); }
    for (int Index = 0; Index < NetDelta::MaxEntities; ++Index)
    {
        const auto* Entity = World.Get(Index);
        if (!Entity || !Entity->Ser || !Tracked(Entity->Ser->Name)) continue;
        const auto* Owner = Entity->Get("m_nSoleControllingPlayer");
        const auto* ServerModel = Entity->Get(ModelField); if (!ServerModel) continue;
        // Shared/unknown controllers never inherit our local selection.
        if (!CourierPolicy::IsOwn(Snapshot.Player, Owner != nullptr, Owner ? Owner->U : 0))
        {
            if (Applied.count(Index))
            {
                Set(*Entity, ModelField, ServerModel->U, Edits[Index]);
                Set(*Entity, MaterialField, Entity->GetUint(MaterialField), Edits[Index]);
                Set(*Entity, MeshField, Entity->GetUint(MeshField), Edits[Index]);
                NetSkins::RecordReplacement(Index, 0, ServerModel->U);
                Applied.erase(Index); LastEdit.erase(Index);
                Log("Courier ownership changed: entity=%d restored server model", Index);
            }
            continue;
        }
        const auto* Team = Entity->Get("m_iTeamNum"); const auto* Flying = Entity->Get("m_bFlyingCourier");
        if (!Team || !Flying) continue;
        const int Branch = CourierPolicy::Branch(static_cast<int>(Team->U), Flying->U != 0); if (Branch < 0) continue;
        const auto& Want = Snapshot.Models[Branch];
        const auto Desired = Want.Id ? Want.Id : ServerModel->U;
        if (!Want.Id && !Applied.count(Index)) continue;
        if (Want.Id && Want.Id != ServerModel->U)
            NetSkins::TranslateReplacement(View, Index, *Entity, ServerModel->U, Want.Id, Edits[Index]);
        NetDelta::FieldPath ModelPath; if (!Entity->Ser->FindPath(ModelField, ModelPath)) continue;
        bool ServerChanged = View.Created(Index);
        if (const auto It = View.Changed().find(Index); It != View.Changed().end())
            for (const auto& P : It->second) if (P == ModelPath) ServerChanged = true;
        auto* Entities = CGameEntitySystem::Get(); auto* Native = Entities ? Entities->Entity(Index) : nullptr;
        const bool Drift = Native && Native->ModelId() != Desired && View.Tick > LastEdit[Index];
        if (Applied.count(Index) && Applied[Index] == Desired && !ServerChanged && !Drift) continue;
        const bool SelectionChanged = !Applied.count(Index) || Applied[Index] != Desired;
        Set(*Entity, ModelField, Desired, Edits[Index]);
        Set(*Entity, MaterialField, Want.Id ? Want.Material : Entity->GetUint(MaterialField), Edits[Index]);
        Set(*Entity, MeshField, Want.Id ? ~0ull : Entity->GetUint(MeshField), Edits[Index]);
        Applied[Index] = Desired; LastEdit[Index] = View.Tick;
        NetSkins::RecordReplacement(Index, Want.Id, ServerModel->U);
        if (SelectionChanged || ServerChanged)
            Log("Courier local model: entity=%d player=%d team=%d flying=%d -> %s", Index, Snapshot.Player, static_cast<int>(Team->U), Flying->U != 0, Want.Id ? Want.Path.c_str() : "server");
        if (!Want.Id) { Applied.erase(Index); LastEdit.erase(Index); }
    }
}
