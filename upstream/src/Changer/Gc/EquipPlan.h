#pragma once
#include "base_gcmessages.pb.h"
#include "econ_gcmessages.pb.h"
#include <cstdint>
#include <vector>
namespace EquipPlan
{
    struct Result
    {
        CMsgClientToGCEquipItems Real;
        std::vector<CSOEconItem> Modified;
        std::vector<std::uint64_t> Unknown;
        bool Consumed = false;
    };
    Result Build(const CMsgClientToGCEquipItems& Request, const std::vector<CSOEconItem>& Items,
                 bool (*IsFake)(std::uint64_t));
}
