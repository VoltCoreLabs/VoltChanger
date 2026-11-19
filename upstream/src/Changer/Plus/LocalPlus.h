#pragma once
#include <cstdint>
#include <string>
namespace LocalPlus
{
    bool Enabled();
    void Update();
    bool Rewrite(std::uint32_t EMsg, std::string& Body);
}
