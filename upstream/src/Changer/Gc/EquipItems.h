#pragma once

#include <cstdint>
#include <vector>

namespace EquipItems
{
    bool OnRequest(const void* Data, std::uint32_t Size, std::vector<std::uint8_t>& Forward);
    bool OnAdjustState(const void* Data, std::uint32_t Size);
}
