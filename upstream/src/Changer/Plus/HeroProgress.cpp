#include "HeroProgress.h"
#include "HeroProgressPlan.h"
#include "SDK/Config/Config.h"
#include "SDK/Logger/Logger.h"
#include <mutex>
namespace
{
    std::mutex Mutex; bool Loaded = false;
    std::map<unsigned, unsigned> Values;
    constexpr const char* Key = "plus.hero_levels";
    void Load() { if (!Loaded) { Values = HeroProgressPlan::Parse(Config::Get(Key)); Loaded = true; } }
}
unsigned HeroProgress::Level(unsigned Hero)
{
    std::lock_guard<std::mutex> Lock(Mutex); Load();
    const auto It = Values.find(Hero); return It == Values.end() ? 0 : It->second;
}
bool HeroProgress::SetLevel(unsigned Hero, unsigned Level)
{
    if (!HeroProgressPlan::Valid(Hero, Level)) return false;
    std::lock_guard<std::mutex> Lock(Mutex); Load();
    if (Level) Values[Hero] = Level; else Values.erase(Hero);
    Config::Set(Key, HeroProgressPlan::Save(Values));
    Log("Local hero level selected: hero=%u level=%u", Hero, Level); return true;
}
void HeroProgress::ImportPreviewChoices(const std::map<unsigned, unsigned>& Badges)
{
    std::lock_guard<std::mutex> Lock(Mutex); Load();
    if (Config::Get("plus.hero_levels_migrated") == "1") return;
    for (const auto& [Hero, Index] : Badges)
        if (Index < 6 && HeroProgressPlan::Valid(Hero, 1) && !Values.count(Hero)) Values[Hero] = HeroProgressPlan::Levels[Index];
    if (!Values.empty()) Config::Set(Key, HeroProgressPlan::Save(Values));
    Config::Set("plus.hero_levels_migrated", "1");
}
