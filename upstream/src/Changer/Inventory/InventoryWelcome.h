#pragma once
#include "gcsdk_gcmessages.pb.h"
#include <string>
namespace InventoryWelcome
{
    template<class RewriteCache> bool Rewrite(std::string& Body, RewriteCache Apply)
    {
        CMsgClientWelcome Welcome; if (!Welcome.ParseFromString(Body)) return false;
        bool Changed = false;
        for (auto& Cache : *Welcome.mutable_outofdate_subscribed_caches())
        {
            auto Data = Cache.SerializeAsString();
            if (Apply(Data) && Cache.ParseFromString(Data)) Changed = true;
        }
        if (!Changed) return false;
        Body = Welcome.SerializeAsString(); return true;
    }
}
