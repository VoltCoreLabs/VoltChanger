#include "Changer/Inventory/CatalogPolicy.h"
#include "Changer/Inventory/StartupInventoryPlan.h"
#include "Changer/Inventory/ItemAcknowledgement.h"
#include "Changer/Inventory/ManualInventoryPolicy.h"
#include "Changer/Inventory/InventoryQueuePlan.h"
#include "Changer/Match/CourierPolicy.h"
#include <iostream>
#include <stdexcept>
void Check(bool Value, const char* Why) { if (!Value) throw std::runtime_error(Why); }
int main()
{
    try
    {
        using CatalogPolicy::Accept;
        Check(InventoryQueuePlan::DiscardOnSubscription(true, 0x8000001au)
              && !InventoryQueuePlan::DiscardOnSubscription(false, 0x8000001au)
              && !InventoryQueuePlan::DiscardOnSubscription(true, 21)
              && !InventoryQueuePlan::DiscardOnSubscription(true, 2570),
              "cache rebuild may cancel only synthetic inventory deltas, never real GC traffic or pending equip responses");
        using ManualInventoryPolicy::Archive;
        Check(Archive(true, true, false, false, 0, false), "untouched auto-catalog entries should leave active inventory");
        Check(!Archive(true, true, true, false, 0, false) && !Archive(true, true, false, true, 0, false)
              && !Archive(true, true, false, false, 2, false) && !Archive(true, true, false, false, 0, true),
              "manual baseline, equipped items, styles and extra attributes must survive automatic-catalog cleanup");
        Check(!Archive(false, true, false, false, 0, false) && !Archive(true, false, false, false, 0, false),
              "noncatalog items and variant IDs must never be swept into the automatic-catalog archive");
        Check(ItemAcknowledgement::Pending(0) && ItemAcknowledgement::Pending(0x40000004u)
              && !ItemAcknowledgement::Pending(73) && !ItemAcknowledgement::Pending(0x80000001u),
              "native unacknowledged inventory markers must be distinguished from ordinary positions");
        Check(ItemAcknowledgement::Position(0, 11946) == 11946
              && ItemAcknowledgement::Position(0x40000004u, 73) == 73
              && ItemAcknowledgement::Position(42, 73) == 42
              && !ItemAcknowledgement::Pending(ItemAcknowledgement::Position(0, 0)),
              "restored and newly granted local items must be acknowledged while preserving existing acknowledged positions");
        std::size_t Initial = 0, Deferred = 0, Unequipped = 0;
        for (std::size_t I = 0; I < 13445; ++I)
        {
            const bool Equipped = I % 1000 == 0;
            if (StartupInventoryPlan::Initial(Equipped, Unequipped)) ++Initial; else ++Deferred;
            if (!Equipped) ++Unequipped;
        }
        Check(Initial == 46 && Initial + Deferred == 13445,
              "full catalog must retain all 14 equipped items while limiting startup to 32 other items");
        Check(!StartupInventoryPlan::Ready(9999, 0, true) && !StartupInventoryPlan::Ready(3999, 1000, true)
              && !StartupInventoryPlan::Ready(4000, 1000, false) && StartupInventoryPlan::Ready(4000, 1000, true),
              "background catalog requires a ready dashboard, delay and known SO owner");
        Check(StartupInventoryPlan::Batch <= 64 && StartupInventoryPlan::Interval >= 100,
              "background publication must remain bounded per frame and rate limited");
        Check(Accept(1234, 1234, 74, 0, false, false), "manifest hero cosmetic must validate against exact native definition and slot");
        Check(Accept(1234, 1234, 1000, 2, false, true), "courier global class must be admitted");
        Check(Accept(1234, 1234, 0, 14, false, true), "legacy global metadata remains supported");
        Check(!Accept(1234, 1, 1000, 2, false, true), "missing definition must not fall back to a different default item");
        Check(!Accept(1234, 1234, 1000, 2, true, true), "bundle containers must never be granted");
        Check(!Accept(1234, 1234, 0, 0, false, false), "tool/no-hero definitions must not enter hero cosmetics");
        Check(!Accept(1234, 1234, 74, 32, false, false), "unsupported/no-loadout slots must not be admitted");
        Check(!Accept(70000, 70000, 74, 0, false, false), "out-of-range native ushort definitions must be rejected");
        Check(CourierPolicy::IsOwn(0, true, 0), "player zero is a valid courier owner");
        Check(!CourierPolicy::IsOwn(0, true, 1) && !CourierPolicy::IsOwn(0, false, 0), "foreign and unavailable ownership must fail closed");
        Check(!CourierPolicy::IsOwn(-1, true, 0) && !CourierPolicy::IsOwn(24, true, 24) && !CourierPolicy::IsOwn(0, true, UINT64_MAX), "invalid and shared-control sentinel owners must fail closed");
        Check(CourierPolicy::Branch(2, false) == 0 && CourierPolicy::Branch(3, false) == 1 && CourierPolicy::Branch(2, true) == 2 && CourierPolicy::Branch(3, true) == 3 && CourierPolicy::Branch(0, true) == -1,
              "ground/flying and radiant/dire models must never be mixed");
        std::cout << "Inventory catalog and strict courier ownership policies passed\n";
    }
    catch (const std::exception& Error) { std::cerr << Error.what() << '\n'; return 1; }
}
