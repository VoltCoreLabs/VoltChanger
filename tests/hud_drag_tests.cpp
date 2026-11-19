#include "Changer/Plus/HudDrag.h"
#include <stdexcept>
#include <iostream>
#include <limits>

void Check(bool Value, const char* Message) { if (!Value) throw std::runtime_error(Message); }
int main()
{
    try
    {
        HudDrag::State S;
        S.X = 100; S.Y = 100;
        S.Tick(120, 110, true, true, 1920, 1080, 340, 400);
        Check(S.Active && S.X == 100 && S.Y == 100, "drag must preserve the initial pointer offset");
        S.Tick(220, 210, true, false, 1920, 1080, 340, 400);
        Check(S.X == 200 && S.Y == 200, "drag must continue after pointer leaves header");
        Check(S.Tick(220, 210, false, false, 1920, 1080, 340, 400) && !S.Active, "release must stop dragging and request one save");
        Check(!S.Tick(220, 210, false, false, 1920, 1080, 340, 400), "idle must not keep saving");
        S.Tick(0, 0, true, false, 1920, 1080, 340, 400);
        S.Tick(10, 10, true, true, 1920, 1080, 340, 400);
        Check(!S.Active, "a held press entering header must not start drag");
        S.Tick(10, 10, false, false, 1920, 1080, 340, 400);
        S.Tick(210, 210, true, true, 1920, 1080, 340, 400);
        S.Tick(9000, 9000, true, true, 1920, 1080, 340, 400);
        Check(S.X == 1580 && S.Y == 680, "panel must remain inside screen bounds");
        S.Tick(-100, -100, true, false, 1920, 1080, 340, 400);
        Check(S.X == 0 && S.Y == 0, "negative screen coordinates must clamp");
        S.X = std::numeric_limits<double>::quiet_NaN(); S.Y = 900;
        S.Clamp(300, 200, 340, 400);
        Check(S.X == 0 && S.Y == 0, "small viewport and invalid saved coordinates must remain recoverable");
        std::cout << "HUD drag scenarios passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; return 1; }
}
