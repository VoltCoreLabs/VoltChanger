#pragma once
#include <array>
#include <cstdint>
namespace VisibilityBits
{
    using State = std::array<std::uint64_t, 256>;
    inline bool Seen(const State& Bits, unsigned Index) { return Index < 16384 && (Bits[Index / 64] & (std::uint64_t{1} << (Index % 64))); }
}
