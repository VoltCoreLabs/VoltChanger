#include "InventorySync.h"

#include "Changer/Inventory/FakeItems.h"
#include "Changer/Inventory/PersonaCatalog.h"
#include "Changer/Inventory/CosmeticCatalog.h"
#include "Changer/Inventory/StartupInventoryPlan.h"
#include "Changer/Inventory/ItemAcknowledgement.h"
#include "Changer/Inventory/InventoryWelcome.h"
#include "Hooks/GcRecvHook.h"
#include "SDK/Panorama/Panel.h"
#include "SDK/Econ/CDOTAInventoryManager.h"
#include "SDK/Econ/CDOTAPlayerInventory.h"
#include "Changer/Gc/GcEmulator.h"
#include "Changer/Inventory/GemCatalog.h"
#include "Changer/Items/ItemSockets.h"
#include "Changer/Gc/ItemStyles.h"
#include "Changer/Inventory/RealItems.h"
#include "Changer/Unusual/UnusualStore.h"
#include "SDK/GC/CGCClient.h"
#include "SDK/Logger/Logger.h"
#include "SDK/SteamAPI/ISteamUser.h"

#include "gcsdk_gcmessages.pb.h"

#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>
#include <deque>
#include <windows.h>

namespace
{
    constexpr int SoTypeEconItem = 1;
    constexpr std::uint32_t SoOwnerTypeUser = 1;
    constexpr std::uint32_t EMsgSoCreate = 21;
    constexpr std::uint32_t EMsgSoUpdate = 22;
    constexpr std::uint32_t EMsgSoDestroy = 23;
    constexpr std::uint32_t EMsgSoCacheSubscribed = 24;
    constexpr std::uint32_t EMsgSoUpdateMultiple = 26;

    std::mutex ShownMutex;
    std::unordered_map<std::uint64_t, std::string> Shown;
    bool Started = false;
    bool Subscribed = false;
    std::mutex PublicationMutex;
    std::deque<std::uint64_t> PendingItems;
    ULONGLONG DashboardSince = 0, NextPublication = 0;
    unsigned PublishedBatches = 0;
    ULONGLONG RestoreAfter = 0;

    bool IsLocalOwner(const CMsgSOIDOwner& Owner)
    {
        ISteamUser* User = ISteamUser::Get();
        return Owner.type() == SoOwnerTypeUser && User && Owner.id() == User->GetSteamID();
    }

    std::set<std::uint32_t> OccupiedSlots()
    {
        std::set<std::uint32_t> Occupied;
        FakeItems::ForEach([&](CSOEconItem& Item) {
            for (const auto& State : Item.equipped_state())
                Occupied.insert((State.new_slot() << 16) | (State.new_class() & 0xFFFF));
        });
        return Occupied;
    }

    CSOEconItem Desired(const CSOEconItem& Pristine, const std::set<std::uint32_t>& Occupied)
    {
        CSOEconItem Out = Pristine;
        auto* States = Out.mutable_equipped_state();
        for (int Index = States->size() - 1; Index >= 0; --Index)
        {
            const auto& State = States->Get(Index);
            if (Occupied.count((State.new_slot() << 16) | (State.new_class() & 0xFFFF)))
                States->DeleteSubrange(Index, 1);
        }
        if (int Effect = UnusualStore::Get(Pristine.id()))
            ItemSockets::SetUnusual(Out, Effect, Pristine.quality());
        return Out;
    }

    bool ReplaceReal(std::string& Data, const std::set<std::uint32_t>& Occupied)
    {
        CSOEconItem Item;
        if (!Item.ParseFromString(Data) || !Item.id() || FakeItems::IsFake(Item.id()))
            return false;
        RealItems::Set(Item);
        std::string Out = Desired(Item, Occupied).SerializeAsString();
        {
            std::lock_guard<std::mutex> Lock(ShownMutex);
            Shown[Item.id()] = Out;
        }
        if (Out == Data)
            return false;
        Data = std::move(Out);
        return true;
    }

    void Forget(const std::string& Data)
    {
        CSOEconItem Item;
        if (!Item.ParseFromString(Data) || !Item.id())
            return;
        RealItems::Remove(Item.id());
        std::lock_guard<std::mutex> Lock(ShownMutex);
        Shown.erase(Item.id());
    }

    std::vector<CSOEconItem> PreparedFakes()
    {
        CosmeticCatalog::UseManualInventory();
        std::vector<CSOEconItem> Items = FakeItems::All();
        auto* User = ISteamUser::Get();
        unsigned Acknowledged = 0;
        for (CSOEconItem& Item : Items)
        {
            if (User) Item.set_account_id(User->AccountId());
            if (ItemAcknowledgement::Pending(Item.inventory())) ++Acknowledged;
            Item.set_inventory(ItemAcknowledgement::Position(Item.inventory(), Item.def_index()));
            ItemStyles::UnlockAll(Item);
            int Effect = UnusualStore::Get(Item.id());
            if (Effect || ItemSockets::FindGem(Item, ItemSockets::UnusualGemDef))
                ItemSockets::SetUnusual(Item, Effect, ItemSockets::QualityUnique);
        }
        FakeItems::Store(Items);
        if (Acknowledged) Log("Inventory local acknowledgement: %u restored items marked already viewed", Acknowledged);
        return Items;
    }

    void ScheduleRestoration()
    {
        auto Items = PreparedFakes();
        std::deque<std::uint64_t> Deferred;
        for (const auto& Item : Items) if (Item.equipped_state_size()) Deferred.push_back(Item.id());
        for (const auto& Item : Items) if (!Item.equipped_state_size()) Deferred.push_back(Item.id());
        std::lock_guard<std::mutex> Lock(PublicationMutex);
        // Do not deliver old synthetic deltas against a newly rebuilt cache.
        // Queue entries retain their provenance; real GC messages are untouched.
        GcRecvHook::DiscardInventoryUpdates();
        PendingItems = std::move(Deferred);
        DashboardSince = NextPublication = 0; PublishedBatches = 0;
        RestoreAfter = GetTickCount64() + 1000;
        Log("Inventory restoration scheduled: %zu saved records after native subscription settles", PendingItems.size());
    }

    void OnUnusual(std::uint64_t ItemId, int Effect)
    {
        if (!FakeItems::IsFake(ItemId))
        {
            InventorySync::Refresh();
            return;
        }
        auto Item = FakeItems::Find(ItemId);
        if (!Item)
            return;
        ItemSockets::SetUnusual(*Item, Effect, ItemSockets::QualityUnique);
        FakeItems::Store(*Item);
        GcEmulator::SendItems({}, {*Item});
    }

    void EnsureSubscribed()
    {
        if (Subscribed)
            return;
        Subscribed = true;
        UnusualStore::Subscribe(OnUnusual);
    }

    bool RewriteSubscribed(std::string& Body)
    {
        CMsgSOCacheSubscribed Message;
        if (!Message.ParseFromString(Body) || !IsLocalOwner(Message.owner_soid()))
            return false;

        EnsureSubscribed();
        Started = true;
        RealItems::Clear();
        {
            std::lock_guard<std::mutex> Lock(ShownMutex);
            Shown.clear();
        }

        std::set<std::uint32_t> Occupied = OccupiedSlots();
        for (auto& Type : *Message.mutable_objects())
        {
            if (Type.type_id() != SoTypeEconItem)
                continue;
            for (std::string& Data : *Type.mutable_object_data())
                ReplaceReal(Data, Occupied);
        }

        ScheduleRestoration();

        Body = Message.SerializeAsString();
        return true;
    }

    bool RewriteMultiple(std::string& Body)
    {
        CMsgSOMultipleObjects Message;
        if (!Message.ParseFromString(Body) || !IsLocalOwner(Message.owner_soid()))
            return false;

        std::set<std::uint32_t> Occupied = OccupiedSlots();
        bool Changed = false;
        for (auto& Object : *Message.mutable_objects_added())
            if (Object.type_id() == SoTypeEconItem)
                Changed |= ReplaceReal(*Object.mutable_object_data(), Occupied);
        for (auto& Object : *Message.mutable_objects_modified())
            if (Object.type_id() == SoTypeEconItem)
                Changed |= ReplaceReal(*Object.mutable_object_data(), Occupied);
        for (const auto& Object : Message.objects_removed())
            if (Object.type_id() == SoTypeEconItem)
                Forget(Object.object_data());
        if (!Changed)
            return false;
        Body = Message.SerializeAsString();
        return true;
    }

    bool RewriteSingle(std::uint32_t EMsg, std::string& Body)
    {
        CMsgSOSingleObject Message;
        if (!Message.ParseFromString(Body) || !IsLocalOwner(Message.owner_soid()) || Message.type_id() != SoTypeEconItem)
            return false;
        if (EMsg == EMsgSoDestroy)
        {
            Forget(Message.object_data());
            return false;
        }
        if (!ReplaceReal(*Message.mutable_object_data(), OccupiedSlots()))
            return false;
        Body = Message.SerializeAsString();
        return true;
    }
}

void InventorySync::Update()
{
    if (Started || !CGCClient::Get() || !ISteamUser::Get())
        return;
    Started = true;
    EnsureSubscribed();
    ScheduleRestoration();
}

void InventorySync::PublishCatalog()
{
    const auto Now = GetTickCount64();
    {
        std::lock_guard<std::mutex> Lock(PublicationMutex);
        if (PendingItems.empty() || Now < NextPublication || Now < RestoreAfter) return;
        NextPublication = Now + StartupInventoryPlan::Interval;
    }
    // Called after the native frame: no bulk inventory work may block the
    // first dashboard frame or run inside an incoming SO-cache callback.
    Panorama::Scope Scope;
    if (!Scope.IsValid()) return;
    auto Root = Panel::Root(); const bool Dashboard = Root.IsValid(); Root.Release();
    if (!Dashboard) return;
    const auto Owner = GcRecvHook::Owner();
    auto* Manager = CDOTAInventoryManager::Get();
    auto* Inventory = Manager ? Manager->LocalInventory() : nullptr;
    if (!Owner.Valid || !Inventory) return;
    std::vector<std::uint64_t> Ids;
    bool Finished = false;
    {
        std::lock_guard<std::mutex> Lock(PublicationMutex);
        if (Now < RestoreAfter) return;
        while (!PendingItems.empty() && Ids.size() < StartupInventoryPlan::Batch)
        { Ids.push_back(PendingItems.front()); PendingItems.pop_front(); }
        Finished = PendingItems.empty();
        ++PublishedBatches;
        std::vector<CSOEconItem> Added, Modified;
        for (const auto Id : Ids)
            if (auto Item = FakeItems::Find(Id))
            {
                // Never recreate an existing native SO object during restoration.
                if (Inventory->FindItem(Id)) Modified.push_back(std::move(*Item));
                else Added.push_back(std::move(*Item));
            }
        GcEmulator::SendItems(Added, Modified);
        if (PublishedBatches == 1 || Finished) Log("Inventory settled restoration: batch=%u create=%zu update=%zu remaining=%zu", PublishedBatches, Added.size(), Modified.size(), PendingItems.size());
    }
    if (Finished) Refresh();
}

void InventorySync::Refresh()
{
    std::set<std::uint32_t> Occupied = OccupiedSlots();
    std::vector<CSOEconItem> Modified;
    RealItems::ForEach([&](const CSOEconItem& Pristine) {
        CSOEconItem Item = Desired(Pristine, Occupied);
        std::string Data = Item.SerializeAsString();
        std::lock_guard<std::mutex> Lock(ShownMutex);
        std::string& Current = Shown[Pristine.id()];
        if (Current == Data)
            return;
        Current = Data;
        Modified.push_back(Item);
    });

    if (Modified.empty())
        return;
    GcEmulator::SendItems({}, Modified);
}

bool InventorySync::Rewrite(std::uint32_t EMsg, std::string& Body)
{
    if (EMsg == 4004)
    {
        if (!InventoryWelcome::Rewrite(Body, RewriteSubscribed)) return false;
        Log("Inventory restored inside GC welcome cache (equipment preserved)");
        return true;
    }
    switch (EMsg)
    {
    case EMsgSoCacheSubscribed:
        return RewriteSubscribed(Body);
    case EMsgSoUpdateMultiple:
        return RewriteMultiple(Body);
    case EMsgSoCreate:
    case EMsgSoUpdate:
    case EMsgSoDestroy:
        return RewriteSingle(EMsg, Body);
    default:
        return false;
    }
}
