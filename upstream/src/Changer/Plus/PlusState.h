#pragma once
#include <cstdint>
#include <string>
namespace PlusState
{
    constexpr int SoType = 2012;
    std::string LocalAccount(std::uint32_t AccountId, std::uint32_t Now);
    bool Rewrite(std::uint32_t EMsg, std::string& Body, std::uint64_t SteamId, std::uint32_t Now);
}
