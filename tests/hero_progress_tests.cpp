#include "Changer/Plus/HeroProgressPlan.h"
#include "Changer/Plus/HeroPageIdentity.h"
#include <iostream>
#include <stdexcept>
void Check(bool Value, const char* Message) { if (!Value) throw std::runtime_error(Message); }
int main()
{
    try
    {
        using namespace HeroProgressPlan;
        Check(HeroPageIdentity::SameTitle("ALCHEMIST", "Alchemist") && HeroPageIdentity::SameTitle("  Alchemist\r\n", "ALCHEMIST"),
              "native upper-case hero headings must resolve independently of localization case and outer whitespace");
        Check(HeroPageIdentity::SameTitle("АНТИМАГ", "Антимаг") && !HeroPageIdentity::SameTitle("AXE", "AXE ARCANA")
              && !HeroPageIdentity::SameTitle("", ""), "localized Unicode headings must match exact names without prefix or empty-name collisions");
        auto Data = Parse("74 30;1 6;0 30;65536 25;2 31;-1 30;3 -5;4 12junk;74 25;");
        Check(Data.size() == 2 && Data.at(74) == 25 && Data.at(1) == 6, "invalid levels and malformed records must not overwrite valid per-hero choices");
        Check(Parse(Save(Data)) == Data, "levels must survive a stable ordered cache roundtrip");
        Check(Valid(74, 0) && !Valid(0, 30) && !Valid(74, 31), "zero restores native progression; invalid heroes/levels must be rejected");
        for (unsigned I = 0; I < 6; ++I) Check(Index(Levels[I]) == I, "tier threshold/index mapping failed");
        const auto Custom = Parse("74 8;1 29;");
        Check(Custom.at(74) == 8 && Custom.at(1) == 29 && Parse(Save(Custom)) == Custom,
              "custom levels between rank thresholds must persist independently for each hero");
        Check(Index(8) == 1 && Index(24) == 3 && Index(29) == 4,
              "custom levels must retain the native rank below the next threshold");
        Check(Parse("").empty(), "missing saved selection must keep native level");
        Check(OwnPlayer(2, 2, 74, 30) && !OwnPlayer(3, 2, 74, 30) && !OwnPlayer(2, -1, 74, 30), "badge override must apply only to the actual local player");
        Check(!OwnPlayer(2, 2, 0, 30) && !OwnPlayer(2, 2, 74, 0), "missing hero and restore-native selection must never fake progression");
        std::cout << "Hero level preferences and tier threshold scenarios passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; return 1; }
}
