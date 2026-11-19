#pragma once

namespace AssistantPlayerData
{
    struct Snapshot
    {
        bool NetWorthAvailable = false;
        int NetWorth = 0;
        bool DamageAvailable = false;
        double Physical = 0;
        double Magical = 0;
        double Pure = 0;
    };

    // Reads only the live local player's own record. Unavailable fields stay
    // unavailable; zero is never substituted for missing or unverified data.
    Snapshot Read(int LocalPlayerId);
}
