#pragma once
#include <cstdint>
namespace CourierPolicy
{
    constexpr bool IsOwn(int LocalPlayer, bool HasOwner, std::uint64_t Owner)
    {
        return LocalPlayer >= 0 && LocalPlayer < 24 && HasOwner && Owner == static_cast<unsigned>(LocalPlayer);
    }
    constexpr int Branch(int Team, bool Flying)
    {
        return Team != 2 && Team != 3 ? -1 : (Flying ? 2 : 0) + (Team == 3 ? 1 : 0);
    }
}
