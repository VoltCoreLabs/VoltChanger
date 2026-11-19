#pragma once
#include <map>
namespace HeroProgress
{
    unsigned Level(unsigned Hero);
    bool SetLevel(unsigned Hero, unsigned Level);
    void ImportPreviewChoices(const std::map<unsigned, unsigned>& Badges);
}
