#pragma once

#include <cstdint>

class CNetMessage;
class C_EconItemView;

namespace EmblemEffect
{
    void Update();
    void OnProjectile(CNetMessage* Message, bool Located);
    bool IsLocalEmblem(const C_EconItemView* View);
}
