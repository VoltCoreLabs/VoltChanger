#include "LoadoutOverride.h"

#include "Changer/Inventory/FakeItems.h"
#include "SDK/Econ/CDOTAInventoryManager.h"
#include "SDK/Econ/CDOTAPlayerInventory.h"
#include "SDK/Econ/C_EconItemView.h"
#include "SDK/Econ/CEconItemSchema.h"
#include "SDK/Econ/CEconItemDefinition.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include "SDK/Logger/Logger.h"
#include "SDK/SteamAPI/ISteamUser.h"
#include "Misc/Offsets.h"

#include <mutex>
#include <set>
#include <tuple>
#include <memory>
#include <vector>

namespace
{
    std::mutex LoggedMutex;
    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t>> Logged;

    C_EconItemView* CachedTerrain(std::uint64_t Id, std::uint32_t Account)
    {
        // A map may request terrain before the SO inventory has received its
        // restored items. Keep native definition/style views alive for the
        // entire process; the regular inventory view takes precedence later.
        struct NativeView
        {
            std::uint64_t Id = 0;
            std::uint32_t Account = 0, Style = 0, Definition = 0;
            alignas(16) std::uint8_t Storage[Offsets::C_EconItemView::Size]{};
            C_EconItemView* View() { return reinterpret_cast<C_EconItemView*>(Storage); }
        };
        static std::mutex Mutex;
        static auto* Views = new std::vector<std::unique_ptr<NativeView>>;
        const auto Item = FakeItems::Find(Id); if (!Item) return nullptr;
        const auto Style = Item->style() == 255 ? 0 : Item->style();
        if (Style > 254) return nullptr;
        std::lock_guard<std::mutex> Lock(Mutex);
        for (auto& Entry : *Views)
            if (Entry->Id == Id && Entry->Account == Account && Entry->Style == Style && Entry->Definition == Item->def_index())
            {
                if (!Entry->Storage[0x30])
                {
                    Entry->View()->Initialize(Entry->Definition, Account, static_cast<std::uint8_t>(Style));
                    Entry->View()->SetItemId(Id);
                }
                return Entry->Storage[0x30] ? Entry->View() : nullptr;
            }
        if (Views->size() >= 64) return nullptr;
        auto Entry = std::make_unique<NativeView>();
        Entry->Id = Id; Entry->Account = Account; Entry->Style = Style; Entry->Definition = Item->def_index();
        C_EconItemView::Construct(Entry->View());
        Entry->View()->Initialize(Entry->Definition, Account, static_cast<std::uint8_t>(Style));
        Entry->View()->SetItemId(Id);
        auto* View = Entry->View(); Views->push_back(std::move(Entry));
        Log("Terrain loadout: restored native view before SO inventory, def=%u style=%u initialized=%u", Item->def_index(), Style, static_cast<unsigned>(reinterpret_cast<const std::uint8_t*>(View)[0x30]));
        auto* Schema = CEconItemSchema::Get(); auto* Definition = Schema ? Schema->GetItemDefinition(Item->def_index()) : nullptr;
        if (Definition) for (const auto& Modifier : Definition->AssetModifiers(Style))
            if (Modifier.Type == "map_override") Log("Terrain map override: %s -> %s", Modifier.Asset.c_str(), Modifier.Modifier.c_str());
        return reinterpret_cast<const std::uint8_t*>(View)[0x30] ? View : nullptr;
    }

    std::uint64_t EquippedFake(std::uint32_t HeroId, std::uint32_t Slot)
    {
        std::uint64_t Found = 0;
        FakeItems::ForEach([&](CSOEconItem& Item) {
            for (const auto& State : Item.equipped_state())
                if (State.new_class() == HeroId && State.new_slot() == Slot)
                    Found = Item.id();
        });
        return Found;
    }

    void LogOnce(std::uint32_t HeroId, std::uint32_t Slot, std::uint64_t From, std::uint64_t To, bool Found)
    {
        std::lock_guard<std::mutex> Lock(LoggedMutex);
        if (!Logged.insert({HeroId, Slot, From, To}).second)
            return;
        Log("Loadout hero %u slot %u %#llx -> %#llx %s", HeroId, Slot, static_cast<unsigned long long>(From), static_cast<unsigned long long>(To),
            Found ? "fake" : "fake not in local inventory");
    }
}

C_EconItemView* LoadoutOverride::SelectedTerrainView()
{
    auto* User = ISteamUser::Get(); if (!User || !User->AccountId()) return nullptr;
    auto Id = EquippedFake(1000, 14); if (!Id) Id = EquippedFake(0, 14);
    return Id ? CachedTerrain(Id, User->AccountId()) : nullptr;
}

C_EconItemView* LoadoutOverride::Apply(CDOTAPlayerInventory* Inventory, std::uint32_t HeroId, std::uint32_t Slot, C_EconItemView* Current)
{
    ISteamUser* User = ISteamUser::Get();
    if (!Inventory || !User || Inventory->OwnerAccountId() != User->AccountId())
        return Current;

    C_DOTA_BaseNPC_Hero* LocalHero = C_DOTAPlayerController::LocalHero();
    std::uint32_t LocalHeroId = LocalHero ? LocalHero->HeroId() : 0;
    if (!AcceptsClass(LocalHeroId, HeroId))
        return Current;

    std::uint64_t FakeId = EquippedFake(HeroId, Slot);
    if (!FakeId)
        return Current;
    std::uint64_t CurrentId = Current ? Current->ItemId() : 0;
    if (CurrentId == FakeId)
        return Current;

    CDOTAInventoryManager* Manager = CDOTAInventoryManager::Get();
    CDOTAPlayerInventory* Local = Manager ? Manager->LocalInventory() : nullptr;
    C_EconItemView* Fake = Local ? Local->FindItem(FakeId) : nullptr;
    if (!Fake && Local != Inventory)
        Fake = Inventory->FindItem(FakeId);
    if (!Fake && (HeroId == 1000 || HeroId == 0) && Slot == 14)
        Fake = CachedTerrain(FakeId, User->AccountId());
    LogOnce(HeroId, Slot, CurrentId, FakeId, Fake != nullptr);
    return Fake ? Fake : Current;
}
