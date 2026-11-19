#include "Changer/Gc/EquipPlan.h"
#include "Changer/Match/LoadoutOverride.h"
#include "Changer/Match/TerrainScope.h"
#include <iostream>
#include <stdexcept>

constexpr std::uint64_t FakeBase = 0x7000000000000000ull;
bool IsFake(std::uint64_t Id) { return Id >> 56 == FakeBase >> 56; }
void Check(bool Condition, const char* Message) { if (!Condition) throw std::runtime_error(Message); }
CSOEconItem Item(unsigned Def, bool Equipped = false)
{
    CSOEconItem Out; Out.set_id(FakeBase | Def); Out.set_def_index(Def); Out.set_style(2);
    if (Equipped) { auto* State = Out.add_equipped_state(); State->set_new_class(1); State->set_new_slot(21); }
    return Out;
}
void Equip(CMsgClientToGCEquipItems& Request, std::uint64_t Id, unsigned Slot = 21)
{
    auto* Entry = Request.add_equips(); Entry->set_item_id(Id); Entry->set_new_class(1); Entry->set_new_slot(Slot);
}
const CSOEconItem& Find(const EquipPlan::Result& Result, std::uint64_t Id)
{
    for (const auto& Item : Result.Modified) if (Item.id() == Id) return Item;
    throw std::runtime_error("expected modified item missing");
}
int main()
{
    try
    {
        for (const char* Name : {"dota", "maps/dota.vmap", "maps/dota.vmap_c", "MAPS\\DOTA.VMAP"})
            Check(TerrainScope::IsBaseMap(Name), "standard Dota resource aliases must be recognized");
        for (const char* Name : {"dota_custom", "maps/custom/dota.vmap", "maps/hero_demo.vmap", "maps/dota_reef.vmap", "../maps/dota.vmap", ""})
            Check(!TerrainScope::IsBaseMap(Name), "custom, preview, already-replaced and traversal maps must not trigger fallback");
        Check(!TerrainScope::IsBaseMap(nullptr), "null map name must not trigger fallback");
        int Existing = 1, Selected = 2; int* Substitution = nullptr; unsigned Calls = 0;
        auto Current = [&] { ++Calls; return &Selected; };
        auto Retry = [&](int* View) { ++Calls; return View == &Selected; };
        Check(TerrainScope::TryCurrentSelection(true, true, true, "dota", &Existing, Substitution, Current, Retry) && Calls == 0 && !Substitution,
              "successful native map choice must not be replaced");
        Check(!TerrainScope::TryCurrentSelection(false, false, true, "dota", &Existing, Substitution, Current, Retry) && Calls == 0,
              "resolver calls outside the accepted native asset callback must not bypass its gates");
        Check(!TerrainScope::TryCurrentSelection(false, true, true, "maps/custom.vmap", &Existing, Substitution, Current, Retry) && Calls == 0,
              "custom-map rejection must not query or apply local terrain");
        Check(TerrainScope::TryCurrentSelection(false, true, true, "maps/dota.vmap", &Existing, Substitution, Current, Retry) && Substitution == &Selected && Calls == 2,
              "failed native choice must retry current selected view and preserve exact used-item identity");
        Substitution = nullptr; Calls = 0;
        Check(!TerrainScope::TryCurrentSelection(false, true, true, "dota", &Selected, Substitution, Current, Retry) && Calls == 1 && !Substitution,
              "same failed view must not be retried or reported as substituted");
        Check(!TerrainScope::TryCurrentSelection(false, true, true, "dota", &Existing, Substitution, Current, [](auto*) { return false; }) && !Substitution,
              "failed retry must not report an accepted replacement");
        Check(LoadoutOverride::AcceptsClass(74, 0), "account-wide terrain/weather loadout must remain accessible after hero spawn");
        Check(LoadoutOverride::AcceptsClass(74, 1000), "current class 1000 account loadout must remain accessible after hero spawn");
        Check(LoadoutOverride::AcceptsClass(74, 74) && !LoadoutOverride::AcceptsClass(74, 1), "hero-specific loadout must still exclude other heroes");
        Check(LoadoutOverride::AcceptsClass(0, 0), "account loadout must work before selecting a hero");
        auto Terrain = Item(11339); CMsgClientToGCEquipItems Global;
        auto* GlobalEquip = Global.add_equips(); GlobalEquip->set_item_id(Terrain.id()); GlobalEquip->set_new_class(1000); GlobalEquip->set_new_slot(14);
        auto TerrainPlan = EquipPlan::Build(Global, {Terrain}, IsFake);
        Check(TerrainPlan.Consumed && TerrainPlan.Real.equips_size() == 0 && Find(TerrainPlan, Terrain.id()).equipped_state(0).new_class() == 1000 && Find(TerrainPlan, Terrain.id()).equipped_state(0).new_slot() == 14,
              "terrain must be equipped in the account slot, without sending fake item to GC");
        auto A = Item(13783), B = Item(13042);
        CMsgClientToGCEquipItems Request;
        Equip(Request, 123456, 0); Equip(Request, A.id());
        auto Mixed = EquipPlan::Build(Request, {A, B}, IsFake);
        Check(Mixed.Consumed && Mixed.Real.equips_size() == 1 && Mixed.Real.equips(0).item_id() == 123456, "mixed batch must forward only real items");
        Check(Find(Mixed, A.id()).equipped_state_size() == 1, "mixed batch must equip persona locally");
        Check(Find(Mixed, A.id()).style() == 2, "unchanged style must be preserved");

        Request.Clear(); Equip(Request, A.id()); Equip(Request, B.id());
        auto Ordered = EquipPlan::Build(Request, {A, B}, IsFake);
        Check(Ordered.Real.equips_size() == 0 && Ordered.Modified.size() == 1 && Find(Ordered, B.id()).equipped_state_size() == 1, "last fake in a batch must own the slot");

        Request.Clear(); Equip(Request, A.id()); Equip(Request, 123456);
        auto RealWins = EquipPlan::Build(Request, {A}, IsFake);
        Check(RealWins.Modified.empty() && RealWins.Real.equips_size() == 1, "later real equip must remove earlier fake equip");

        A = Item(13783, true); Request.Clear(); Equip(Request, 0);
        auto Removed = EquipPlan::Build(Request, {A}, IsFake);
        Check(Removed.Consumed && Removed.Real.equips_size() == 0 && Find(Removed, A.id()).equipped_state_size() == 0, "unequip must clear a local persona");

        Request.Clear(); Equip(Request, FakeBase | 999999);
        auto Unknown = EquipPlan::Build(Request, {A}, IsFake);
        Check(Unknown.Consumed && Unknown.Real.equips_size() == 0 && Unknown.Unknown.size() == 1, "unknown fake IDs must never reach the server");

        Request.Clear(); Equip(Request, 123456);
        auto RealOnly = EquipPlan::Build(Request, {A}, IsFake);
        Check(!RealOnly.Consumed && Find(RealOnly, A.id()).equipped_state_size() == 0, "real-only equip must clear a conflicting local item and pass through");
        std::cout << "All equip plan scenarios passed\n";
    }
    catch (const std::exception& Error) { std::cerr << Error.what() << '\n'; return 1; }
    return 0;
}
