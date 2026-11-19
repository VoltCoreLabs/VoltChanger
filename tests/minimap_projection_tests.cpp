#include "Changer/Plus/MinimapProjection.h"
#include <stdexcept>
#include <iostream>
#include <limits>
void Check(bool V, const char* T) { if (!V) throw std::runtime_error(T); }
int main()
{
    try
    {
        MinimapProjection::State S{-8192, 8192, 16, 1, 256, 256, 4, 8, 4, 8};
        auto P = MinimapProjection::Project(S, 0, 0, 512, 512);
        Check(P && P->X == 256 && P->Y == 256, "native overview center must map to viewport center");
        P = MinimapProjection::Project(S, -8192, 8192, 512, 512);
        Check(P && P->X == 8 && P->Y == 16, "native insets and layout scaling not applied");
        S.Zoom = 2;
        P = MinimapProjection::Project(S, 4096, 0, 512, 512);
        Check(P && P->X == 504 && P->Y == 256, "zoom must apply around the overview center");
        Check(!MinimapProjection::Project(S, 8192, 0, 512, 512), "points outside native clipping bounds must not be drawn");
        S.Scale = 0; Check(!MinimapProjection::Project(S, 0, 0, 512, 512), "uninitialized map scale accepted");
        S.Scale = 16; S.Left = INT32_MAX; Check(!MinimapProjection::Project(S, 0, 0, 512, 512), "invalid insets accepted");
        S.Left = 4;
        Check(!MinimapProjection::Project(S, 0, 0, std::numeric_limits<double>::infinity(), 512), "nonfinite layout accepted");
        Check(!MinimapProjection::Project(S, std::numeric_limits<double>::quiet_NaN(), 0, 512, 512), "nonfinite observation accepted");
        std::cout << "Native minimap normalization tests passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; return 1; }
}
