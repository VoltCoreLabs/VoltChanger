#pragma once
#include <cstdint>
namespace CatalogPolicy
{
    constexpr bool Accept(std::uint32_t Requested, std::uint32_t Actual, std::uint32_t Hero,
                          std::uint32_t Slot, bool Bundle, bool Global)
    {
        return Requested > 0 && Requested <= 65535 && Requested == Actual && Slot < 32 && !Bundle &&
            (Global ? (Hero == 1000 || Hero == 0) : Hero > 0 && Hero < 1000);
    }
}
