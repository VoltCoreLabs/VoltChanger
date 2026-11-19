#pragma once
#include <optional>
#include <vector>

namespace AssistantMetrics
{
    struct Point { double Time, LastHits, Denies, NetWorth; };
    // Do not invent pre/post-dataset benchmarks. Linear interpolation only within it.
    inline std::optional<Point> At(const std::vector<Point>& Points, double Time)
    {
        if (Points.empty() || Time < Points.front().Time || Time > Points.back().Time) return std::nullopt;
        for (std::size_t Index = 1; Index < Points.size(); ++Index)
        {
            const auto& A = Points[Index - 1]; const auto& B = Points[Index];
            if (Time > B.Time) continue;
            if (B.Time <= A.Time) return std::nullopt;
            const auto Blend = (Time - A.Time) / (B.Time - A.Time);
            return Point{Time, A.LastHits + (B.LastHits-A.LastHits)*Blend, A.Denies + (B.Denies-A.Denies)*Blend, A.NetWorth + (B.NetWorth-A.NetWorth)*Blend};
        }
        return Points.back();
    }
}
