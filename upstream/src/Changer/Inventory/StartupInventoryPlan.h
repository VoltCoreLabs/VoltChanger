#pragma once
#include <cstddef>
#include <cstdint>
namespace StartupInventoryPlan
{
    constexpr std::size_t InitialUnequipped = 32;
    constexpr std::size_t Batch = 48;
    constexpr std::uint64_t Delay = 3000, Interval = 150;
    constexpr bool Initial(bool Equipped, std::size_t UnequippedOrdinal)
    { return Equipped || UnequippedOrdinal < InitialUnequipped; }
    constexpr bool Ready(std::uint64_t Now, std::uint64_t DashboardSince, bool OwnerValid)
    { return OwnerValid && DashboardSince && Now >= DashboardSince && Now - DashboardSince >= Delay; }
}
