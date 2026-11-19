#pragma once
#include <cstdint>
namespace InventoryQueuePlan
{
    constexpr bool DiscardOnSubscription(bool Synthetic, std::uint32_t Message)
    { return Synthetic && (Message & 0x7fffffffu) == 26; }
}
