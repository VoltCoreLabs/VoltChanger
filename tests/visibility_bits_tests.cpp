#include "Changer/Plus/VisibilityBits.h"
#include <stdexcept>
#include <iostream>
int main()
{
    try
    {
        VisibilityBits::State State{};
        State[0] = 1ull | (1ull << 63); State[1] = 1; State[255] = 1ull << 63;
        if (!VisibilityBits::Seen(State, 0) || !VisibilityBits::Seen(State, 63) || !VisibilityBits::Seen(State, 64) || !VisibilityBits::Seen(State, 16383)) throw std::runtime_error("visible index boundary mapping failed");
        if (VisibilityBits::Seen(State, 1) || VisibilityBits::Seen(State, 65) || VisibilityBits::Seen(State, 16384) || VisibilityBits::Seen(State, UINT32_MAX)) throw std::runtime_error("hidden/out-of-range entity accepted");
        std::cout << "Visibility bitset boundaries passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; return 1; }
}
