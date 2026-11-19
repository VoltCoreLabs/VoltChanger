#pragma once
#include "SDK/NetDelta/EntityWorld.h"
namespace CourierSkins
{
    bool Tracked(const std::string& Name);
    void Update(int LocalPlayer);
    void Edit(const NetDelta::PacketView& View, std::map<int, NetDelta::EntityEdits>& Edits);
}
