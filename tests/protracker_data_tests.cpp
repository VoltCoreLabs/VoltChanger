#include "Changer/Plus/ProTrackerData.h"
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
void Check(bool Value, const char* Why) { if (!Value) throw std::runtime_error(Why); }
int main()
{
    try
    {
        std::ifstream F(OVR_D2PT_FIXTURE, std::ios::binary);
        const std::string Raw(std::istreambuf_iterator<char>(F), {});
        ProTrackerData::Names Items;
        for (unsigned I = 1; I < 3000; ++I) Items[I] = "valid_item_" + std::to_string(I);
        const auto S = ProTrackerData::Parse(Raw, 18, 1, Items, {});
        Check(S.Hero == 18 && S.Role == 1 && S.Matches == 4072 && S.WinRate > 51 && S.WinRate < 52, "recorded hero/role sample and WR must be preserved");
        Check(!S.Stages[0].empty() && S.Stages[0][0].Pick >= S.Stages[0].back().Pick, "starting purchases must retain real fractions as percentages sorted by popularity");
        Check(S.Skills.size() == 10 && S.Skills.front().Level == 1 && S.Skills.front().Name == "sven_storm_bolt", "only actual supplied ability levels should be displayed");
        Check(S.Talents.size() == 4 && S.Talents.front().Level == 25 && S.Talents.back().Level == 10, "talent pairs must be preserved in tree order");
        Check(ProTrackerData::Parse(Raw, 18, 3, Items, {}).Matches == 0 && ProTrackerData::Parse(Raw, 29, 1, Items, {}).Matches == 0,
              "a different position or hero must not silently reuse the available build");
        for (const auto& Stage : S.Stages) for (const auto& I : Stage) Check(I.Pick >= 0 && I.Pick <= 100, "buy rates must stay within percentage bounds");
        bool Failed = false; try { ProTrackerData::Parse("<html>blocked</html>", 18, 1, Items, {}); } catch (...) { Failed = true; }
        Check(Failed, "Cloudflare HTML must never be interpreted as empty successful build data");
        Failed = false; try { ProTrackerData::Parse(Raw, 18, 6, Items, {}); } catch (...) { Failed = true; }
        Check(Failed, "invalid role must fail before processing builds");
        Check(ProTrackerData::Parse("[]", 18, 1, Items, {}).Matches == 0, "absent role data must remain explicitly unavailable");
        std::cout << "D2PT role isolation, items, skills and talents passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; return 1; }
}
