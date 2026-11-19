#include "Changer/Plus/AssistantMetrics.h"
#include <cmath>
#include <iostream>
int main()
{
    using namespace AssistantMetrics;
    const std::vector<Point> Data = {{300,20,3,1700},{600,50,7,3200}};
    const auto Mid = At(Data,450);
    if (!Mid || std::abs(Mid->LastHits-35)>1e-8 || std::abs(Mid->Denies-5)>1e-8 || std::abs(Mid->NetWorth-2450)>1e-8) return 1;
    if (At(Data,299) || At(Data,601) || At({},300)) return 2;
    if (!At(Data,300) || At(Data,300)->LastHits != 20 || At(Data,600)->NetWorth != 3200) return 3;
    if (At({{300,20,3,1700},{300,50,7,3200}},300)) return 4;
    std::cout << "All assistant metric interpolation scenarios passed\n";
}
