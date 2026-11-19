#include "ItemGrant.h"

#include "Changer/Inventory/FakeItems.h"
#include "Changer/Inventory/ItemAcknowledgement.h"
#include "Changer/Gc/GcEmulator.h"
#include "Changer/Items/ItemSockets.h"
#include "Changer/Gc/ItemStyles.h"
#include "SDK/Logger/Logger.h"
#include "SDK/SteamAPI/ISteamUser.h"

#include <cstdint>
#include <vector>

bool ItemGrant::Grant(std::uint32_t Def)
{
    return Grant(std::vector<std::uint32_t>{Def});
}

bool ItemGrant::Grant(const std::vector<std::uint32_t>& Defs, bool Notify)
{
    ISteamUser* User = ISteamUser::Get();
    std::uint32_t AccountId = User ? User->AccountId() : 0;
    if (!AccountId || Defs.empty())
        return false;

    std::vector<CSOEconItem> Added;
    for (std::uint32_t Def : Defs)
    {
        if (!Def)
            continue;
        CSOEconItem Item;
        Item.set_id(FakeItems::IdBase | Def);
        Item.set_account_id(AccountId);
        Item.set_def_index(Def);
        Item.set_inventory(0);
        Item.set_quantity(1);
        Item.set_level(1);
        Item.set_quality(ItemSockets::QualityUnique);

        if (auto Existing = FakeItems::Find(Item.id()))
            Item = *Existing;
        Item.set_inventory(ItemAcknowledgement::Position(Item.inventory(), Def));
        ItemStyles::UnlockAll(Item);
        Added.push_back(Item);
    }
    if (Added.empty())
        return false;

    FakeItems::Store(Added);
    if (Notify) GcEmulator::SendItems(Added, {});
    return true;
}
