#pragma once

#include <cstdint>
#include <string>

namespace InventorySync
{
    void Update();
    void PublishCatalog();
    void Refresh();
    bool Rewrite(std::uint32_t EMsg, std::string& Body);
}
