#include "PersonaCatalog.h"
#include "FakeItems.h"
#include "Changer/Gc/ItemGrant.h"
#include "SDK/Econ/CEconItemSchema.h"
#include "SDK/Econ/CEconItemDefinition.h"
#include "SDK/Logger/Logger.h"
#include <vector>

void PersonaCatalog::EnsureGranted()
{
    static bool Done = false;
    if (Done)
        return;
    auto* Schema = CEconItemSchema::Get();
    if (!Schema)
        return;
    Done = true;
    // Unlock items, not default selector placeholders or individual persona clothes.
    // Validate each candidate against the installed client's schema.
    constexpr std::uint32_t Definitions[] = {13042, 13783, 13786, 18113, 18178, 19205, 22723,
                                            31357, 31367, 36191, 36193, 36214};
    std::vector<std::uint32_t> Missing;
    unsigned Validated = 0;
    for (auto Id : Definitions)
    {
        auto* Def = Schema->GetItemDefinition(Id);
        if (!Def || Def->DefIndex() != Id || !Def->HeroId())
            continue;
        bool Persona = false, Model = false;
        for (const auto& Modifier : Def->AssetModifiers(0))
        {
            Persona |= Modifier.Type == "persona";
            Model |= Modifier.Type == "entity_model" && !Modifier.Modifier.empty();
        }
        if (!Persona || !Model)
            continue;
        ++Validated;
        if (!FakeItems::Find(FakeItems::IdBase | Id))
        {
            Missing.push_back(Id);
            Log("Persona unlock added def %u hero %u slot %u", Id, Def->HeroId(), Def->LoadoutSlot());
        }
    }
    if (!Missing.empty())
        ItemGrant::Grant(Missing);
    Log("Persona catalog supported %u added %zu", Validated, Missing.size());
}
