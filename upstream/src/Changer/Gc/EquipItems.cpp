#include "EquipItems.h"
#include "EquipPlan.h"
#include "Changer/Inventory/FakeItems.h"
#include "Changer/Gc/GcEmulator.h"
#include "Changer/Inventory/InventorySync.h"
#include "Hooks/GcRecvHook.h"
#include "SDK/Logger/Logger.h"
#include <cstring>

namespace
{
    void ApplyLocal(const EquipPlan::Result& Plan)
    {
        for (auto Id : Plan.Unknown)
            LogE("Equip: unknown local item %#llx", static_cast<unsigned long long>(Id));
        if (Plan.Modified.empty()) return;
        FakeItems::Store(Plan.Modified);
        GcEmulator::SendItems({}, Plan.Modified);
        InventorySync::Refresh();
    }
}

bool EquipItems::OnAdjustState(const void* Data, std::uint32_t Size)
{
    CMsgProtoBufHeader Header; CMsgAdjustItemEquippedState Equip;
    if (!GcEmulator::Parse(Data, Size, Header, Equip) || !Equip.has_new_class() || !Equip.has_new_slot()) return false;
    CMsgClientToGCEquipItems Request; *Request.add_equips() = Equip;
    const auto Plan = EquipPlan::Build(Request, FakeItems::All(), FakeItems::IsFake);
    ApplyLocal(Plan);
    Log("Single equip request class %u slot %u local %d changes %zu", Equip.new_class(), Equip.new_slot(), FakeItems::IsFake(Equip.item_id()), Plan.Modified.size());
    // 1059 is the single-item state message. Its SO update is the result;
    // the batched 2570 response belongs only to 2569 requests.
    return Plan.Consumed;
}

bool EquipItems::OnRequest(const void* Data, std::uint32_t Size, std::vector<std::uint8_t>& Forward)
{
    Forward.clear();
    CMsgProtoBufHeader Header;
    CMsgClientToGCEquipItems Request;
    if (!GcEmulator::Parse(Data, Size, Header, Request) || Request.equips_size() == 0)
        return false;
    auto Plan = EquipPlan::Build(Request, FakeItems::All(), FakeItems::IsFake);
    for (const auto& Equip : Request.equips())
        Log("Equip request hero %u slot %u local %d style %u", Equip.new_class(), Equip.new_slot(),
            FakeItems::IsFake(Equip.item_id()), Equip.style_index());
    ApplyLocal(Plan);
    if (!Plan.Consumed)
        return false;
    Log("Equip batch total %d forwarded %d local changes %zu", Request.equips_size(), Plan.Real.equips_size(), Plan.Modified.size());
    if (Plan.Real.equips_size())
    {
        // Keep the original envelope/job ID; the real server supplies one response.
        std::uint32_t HeaderLength = 0;
        std::memcpy(&HeaderLength, static_cast<const std::uint8_t*>(Data) + 4, 4);
        const auto* Bytes = static_cast<const std::uint8_t*>(Data);
        Forward.assign(Bytes, Bytes + 8 + HeaderLength);
        auto Body = Plan.Real.SerializeAsString();
        Forward.insert(Forward.end(), Body.begin(), Body.end());
    }
    else
    {
        CMsgClientToGCEquipItemsResponse Response;
        Response.set_so_cache_version_id(GcRecvHook::Owner().Version);
        GcEmulator::Respond(2570, Header, Response);
    }
    return true;
}
