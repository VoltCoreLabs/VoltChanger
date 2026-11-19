#include "Changer/Plus/ShopGuidePolicy.h"
#include <iostream>
#include <stdexcept>
void Check(bool Value, const char* Message) { if (!Value) throw std::runtime_error(Message); }
int main()
{
    try
    {
        using ShopGuidePolicy::UseOpenDota;
        Check(!UseOpenDota(true, true, false), "Workshop/default guide must retain native content even when OpenDota is enabled");
        Check(UseOpenDota(true, true, true), "explicit native Plus Guide selection enables its local content");
        Check(!UseOpenDota(false, true, true), "opt-out restores native Plus content without selecting a different guide");
        Check(!UseOpenDota(true, false, true), "missing hero must not request a stale build");
        // Native switching is sufficient; previous output visibility is not an input.
        for (bool SelectedPlus : {false, true, false, true, false})
            Check(UseOpenDota(true, true, SelectedPlus) == SelectedPlus, "Plus/Workshop switching must not stick to the previous overlay state");
        std::cout << "Shop native Plus/Workshop routing tests passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; return 1; }
}
