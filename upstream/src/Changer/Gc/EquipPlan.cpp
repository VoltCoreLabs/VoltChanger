#include "EquipPlan.h"
#include <unordered_map>

EquipPlan::Result EquipPlan::Build(const CMsgClientToGCEquipItems& Request, const std::vector<CSOEconItem>& Items,
                                  bool (*IsFake)(std::uint64_t))
{
    Result Out;
    Out.Real = Request;
    Out.Real.clear_equips();
    std::unordered_map<std::uint64_t, CSOEconItem> Working;
    for (const auto& Item : Items)
        Working[Item.id()] = Item;
    for (const auto& Equip : Request.equips())
    {
        const bool Fake = Equip.item_id() && IsFake(Equip.item_id());
        bool Cleared = false;
        // Preserve batch order, including changes made by earlier entries.
        for (auto& [Id, Item] : Working)
        {
            auto* States = Item.mutable_equipped_state();
            for (int Index = States->size() - 1; Index >= 0; --Index)
            {
                const auto& State = States->Get(Index);
                if (State.new_class() == Equip.new_class() && State.new_slot() == Equip.new_slot())
                {
                    States->DeleteSubrange(Index, 1);
                    Cleared = true;
                }
            }
        }
        if (!Fake)
        {
            if (Equip.item_id() || !Cleared)
                *Out.Real.add_equips() = Equip;
            else
                Out.Consumed = true;
            continue;
        }
        Out.Consumed = true;
        auto It = Working.find(Equip.item_id());
        if (It == Working.end())
        {
            Out.Unknown.push_back(Equip.item_id());
            continue;
        }
        auto* State = It->second.add_equipped_state();
        State->set_new_class(Equip.new_class());
        State->set_new_slot(Equip.new_slot());
        if (Equip.style_index() != 255)
            It->second.set_style(Equip.style_index());
    }
    for (const auto& Item : Items)
    {
        const auto& Updated = Working.at(Item.id());
        if (Updated.SerializeAsString() != Item.SerializeAsString())
            Out.Modified.push_back(Updated);
    }
    return Out;
}
