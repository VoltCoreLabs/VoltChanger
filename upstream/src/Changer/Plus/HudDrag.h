#pragma once
#include <algorithm>
#include <cmath>

namespace HudDrag
{
    struct State
    {
        double X = 16, Y = 160, OffsetX = 0, OffsetY = 0;
        bool Active = false, PreviousDown = false;

        void Clamp(double ViewWidth, double ViewHeight, double Width, double Height)
        {
            X = std::clamp(std::isfinite(X) ? X : 16., 0., std::max(0., ViewWidth - Width));
            Y = std::clamp(std::isfinite(Y) ? Y : 160., 0., std::max(0., ViewHeight - Height));
        }
        // All coordinates are in Panorama layout units, after UI scale conversion.
        // Returns true on release so persistence happens once, never each frame.
        bool Tick(double MouseX, double MouseY, bool Down, bool HeaderHovered,
                  double ViewWidth, double ViewHeight, double Width, double Height)
        {
            const bool Released = Active && !Down;
            if (!Down) Active = false;
            else if (!PreviousDown && HeaderHovered)
            {
                Active = true; OffsetX = MouseX - X; OffsetY = MouseY - Y;
            }
            if (Active && std::isfinite(MouseX) && std::isfinite(MouseY))
            {
                X = MouseX - OffsetX; Y = MouseY - OffsetY;
            }
            PreviousDown = Down;
            Clamp(ViewWidth, ViewHeight, Width, Height);
            return Released;
        }
    };
}
