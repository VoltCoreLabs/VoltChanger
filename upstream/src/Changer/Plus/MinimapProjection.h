#pragma once
#include <cmath>
#include <optional>
namespace MinimapProjection
{
    struct State
    {
        float OriginX = 0, OriginY = 0, Scale = 0, Zoom = 0;
        int Width = 0, Height = 0, Left = 0, Top = 0, Right = 0, Bottom = 0;
    };
    struct Point { double X = 0, Y = 0; };
    inline std::optional<Point> Project(const State& S, double WorldX, double WorldY, double LayoutWidth, double LayoutHeight)
    {
        if (!std::isfinite(WorldX) || !std::isfinite(WorldY) || !std::isfinite(S.OriginX) || !std::isfinite(S.OriginY)
            || !std::isfinite(S.Scale) || !std::isfinite(S.Zoom) || S.Scale <= 0 || S.Zoom <= 0 || S.Zoom > 32
            || S.Width < 32 || S.Height < 32 || S.Width > 8192 || S.Height > 8192 || !std::isfinite(LayoutWidth) || !std::isfinite(LayoutHeight) || LayoutWidth <= 0 || LayoutHeight <= 0
            || S.Left < 0 || S.Right < 0 || S.Top < 0 || S.Bottom < 0 || S.Left >= S.Width || S.Right >= S.Width || S.Top >= S.Height || S.Bottom >= S.Height
            || S.Left + S.Right >= S.Width || S.Top + S.Bottom >= S.Height) return {};
        // Exact two-stage native mapping (current client): world -> overview,
        // then overview -> renderer pixels. No assumed world extents.
        const auto U = (WorldX - S.OriginX) / S.Scale, V = (WorldY - S.OriginY) / -S.Scale;
        const auto W = S.Width - S.Left - S.Right, H = S.Height - S.Top - S.Bottom;
        const auto PX = (U - 512) * S.Zoom / 1024 * W + W * .5 + S.Left;
        const auto PY = (V - 512) * S.Zoom / 1024 * H + H * .5 + S.Top;
        if (!std::isfinite(PX) || !std::isfinite(PY) || PX < S.Left || PY < S.Top || PX > S.Width - S.Right || PY > S.Height - S.Bottom) return {};
        return Point{PX / S.Width * LayoutWidth, PY / S.Height * LayoutHeight};
    }
}
