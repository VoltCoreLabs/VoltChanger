#pragma once
#include <cstdint>
namespace ItemAcknowledgement
{
    // Native inventory positions: zero or bit30 denotes an unacknowledged
    // item. Acknowledged positions are nonzero and have bit30 clear.
    constexpr bool Pending(std::uint32_t Position)
    { return Position == 0 || (Position & 0x40000000u) != 0; }
    constexpr std::uint32_t Position(std::uint32_t Saved, std::uint32_t Definition)
    {
        if (!Pending(Saved)) return Saved;
        return Definition > 0 && Definition <= 65535 ? Definition : 1;
    }
}
