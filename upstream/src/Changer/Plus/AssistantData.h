#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace AssistantData
{
    struct Item
    {
        std::uint32_t Id = 0;
        std::string Name; // Internal name accepted by DOTAItemImage, e.g. item_blink.
        double Frequency = 0; // Raw OpenDota source counter, not a player probability.
    };

    struct TimedPoint
    {
        std::uint32_t Time = 0; // Game seconds, from Valve's all-ranks (rank_chunk=0) snapshot.
        double LastHits = 0;
        double Denies = 0;
        double NetWorth = 0;
    };

    struct Snapshot
    {
        std::uint32_t HeroId = 0;
        bool Busy = false;
        std::string ItemsStatus;
        std::string TimedStatsStatus;
        std::string SkillsStatus;
        std::vector<Item> StartingItems;
        std::vector<Item> EarlyItems;
        std::vector<Item> MidItems;
        std::vector<Item> LateItems;
        std::vector<TimedPoint> TimedStats;
        std::vector<std::string> SkillOrder;
        std::uint64_t SourceMatchId = 0;
    };

    // Both functions are safe on the UI/game thread. All HTTP and file parsing happens
    // on a detached process-lifetime worker. Refreshes are limited to once/minute/hero.
    // Hero 0 clears the current display and cancels publishing an in-flight request.
    void SetHero(std::uint32_t HeroId);
    Snapshot Get();
}
