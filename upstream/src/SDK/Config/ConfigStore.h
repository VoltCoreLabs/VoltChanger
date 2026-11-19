#pragma once
#include <filesystem>
#include <map>
#include <string>

class ConfigStore
{
public:
    using Values = std::map<std::string, std::string>;
    enum class Source { Empty, Cache, Backup, Legacy };
    ConfigStore(std::filesystem::path Cache, std::filesystem::path Legacy);
    Source Load(Values& Out) const;
    bool Save(const Values& Data) const;
    const std::filesystem::path& Path() const { return Cache; }
private:
    std::filesystem::path Cache, Legacy;
};
