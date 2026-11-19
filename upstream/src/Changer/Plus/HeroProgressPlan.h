#pragma once
#include <map>
#include <sstream>
#include <string>
namespace HeroProgressPlan
{
    constexpr unsigned Levels[] = {1, 6, 12, 18, 25, 30};
    constexpr bool Valid(unsigned Hero, unsigned Level) { return Hero > 0 && Hero < 65536 && Level <= 30; }
    constexpr bool OwnPlayer(int Requested, int Local, unsigned Hero, unsigned Level)
    {
        return Local >= 0 && Local < 24 && Requested == Local && Valid(Hero, Level) && Level > 0;
    }
    inline std::map<unsigned, unsigned> Parse(const std::string& Text)
    {
        std::map<unsigned, unsigned> Out; std::istringstream Input(Text); std::string Row;
        while (std::getline(Input, Row, ';'))
        {
            std::istringstream Fields(Row); long long Hero = -1, Level = -1; char Extra = 0;
            if (Fields >> Hero >> Level && !(Fields >> Extra) && Hero > 0 && Hero < 65536 && Level > 0 && Level <= 30)
                Out[static_cast<unsigned>(Hero)] = static_cast<unsigned>(Level);
        }
        return Out;
    }
    inline std::string Save(const std::map<unsigned, unsigned>& Values)
    {
        std::string Out;
        for (const auto& [Hero, Level] : Values) if (Valid(Hero, Level) && Level) Out += std::to_string(Hero) + " " + std::to_string(Level) + ";";
        return Out;
    }
    constexpr unsigned Index(unsigned Level)
    {
        unsigned Out = 0; for (unsigned I = 0; I < 6; ++I) if (Level >= Levels[I]) Out = I; return Out;
    }
    constexpr const char* Color(unsigned Level)
    {
        constexpr const char* Colors[] = {"#ad8973", "#aab9c6", "#d0b970", "#8fc7c0", "#bd9bce", "#db897c"};
        return Colors[Index(Level)];
    }
}
