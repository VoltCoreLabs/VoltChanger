#pragma once
#include <string>
#include <vector>
namespace VisionTracker
{
    struct Point { double X = 0, Y = 0, Time = 0; };
    struct Observation { unsigned Handle = 0; std::string Kind; bool Courier = false; std::vector<Point> Trail; };
    struct Snapshot
    {
        std::string Status;
        double Time = 0;
        std::vector<Observation> EnemyObjects;
        std::vector<Point> JungleRoute;
    };
    void Update();
    Snapshot Get();
}
