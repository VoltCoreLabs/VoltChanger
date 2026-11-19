#pragma once
#include <algorithm>
#include <string>
#include <cctype>
namespace TerrainScope
{
    inline bool IsBaseMap(const char* Name)
    {
        if (!Name) return false;
        std::string Path(Name);
        std::replace(Path.begin(), Path.end(), '\\', '/');
        std::transform(Path.begin(), Path.end(), Path.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
        return Path == "dota" || Path == "dota.vmap" || Path == "dota.vmap_c" ||
            Path == "maps/dota" || Path == "maps/dota.vmap" || Path == "maps/dota.vmap_c";
    }
    template<class View, class Select, class Resolve>
    bool TryCurrentSelection(bool NativeResult, bool AssetCallback, bool HasOutput, const char* Input,
                             View* Original, View*& Substituted, Select Current, Resolve Retry)
    {
        if (NativeResult) return true;
        if (!AssetCallback || !HasOutput || !IsBaseMap(Input)) return false;
        auto* Selected = Current();
        if (!Selected || Selected == Original || !Retry(Selected)) return false;
        Substituted = Selected; return true;
    }
}
