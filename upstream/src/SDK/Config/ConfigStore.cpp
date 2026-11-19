#include "ConfigStore.h"
#include <windows.h>
#include <fstream>
#include <sstream>

namespace
{
    constexpr const char* Header = "# OVR persistent inventory v1";
    constexpr const char* Footer = "# OVR end";
    bool Read(const std::filesystem::path& Path, ConfigStore::Values& Out, bool RequireHeader)
    {
        std::error_code Error;
        const auto Size = std::filesystem::file_size(Path, Error);
        if (Error || !Size || Size > 32 * 1024 * 1024) return false;
        std::ifstream File(Path, std::ios::binary); if (!File) return false;
        ConfigStore::Values Parsed; std::string Line; bool Version = false, Complete = false, First = true;
        while (std::getline(File, Line))
        {
            if (First && Line.starts_with("\xef\xbb\xbf")) Line.erase(0, 3);
            First = false;
            if (!Line.empty() && Line.back() == '\r') Line.pop_back();
            if (Complete && !Line.empty()) return false;
            if (Line == Header) { Version = true; continue; }
            if (Line == Footer) { Complete = true; continue; }
            if (Line.empty() || Line.front() == '#') continue;
            const auto Equal = Line.find('=');
            if (!Equal || Equal == std::string::npos || Line.find('\0') != std::string::npos) return false;
            Parsed[Line.substr(0, Equal)] = Line.substr(Equal + 1);
        }
        if (File.bad() || (Version && !Complete) || (RequireHeader && !Version) || (!Version && Parsed.empty())) return false;
        Out = std::move(Parsed); return true;
    }
    bool Write(const std::filesystem::path& Path, const ConfigStore::Values& Data)
    {
        std::error_code Error;
        if (!Path.parent_path().empty()) std::filesystem::create_directories(Path.parent_path(), Error);
        if (Error) return false;
        auto Temporary = Path; Temporary += L".tmp";
        std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
        if (!File) return false;
        File << Header << '\n';
        for (const auto& [Key, Value] : Data)
        {
            if (Key.empty() || Key.find_first_of("=\r\n") != std::string::npos || Value.find_first_of("\r\n") != std::string::npos) return false;
            File << Key << '=' << Value << '\n';
        }
        File << Footer << '\n';
        File.flush(); const bool Good = File.good(); File.close();
        if (!Good || File.fail()) return false;
        return MoveFileExW(Temporary.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
}
ConfigStore::ConfigStore(std::filesystem::path Cache, std::filesystem::path Legacy) : Cache(std::move(Cache)), Legacy(std::move(Legacy)) {}
ConfigStore::Source ConfigStore::Load(Values& Out) const
{
    Out.clear();
    if (Read(Cache, Out, true)) return Source::Cache;
    auto Backup = Cache; Backup += L".bak";
    if (Read(Backup, Out, true)) return Source::Backup;
    if (Read(Legacy, Out, false)) return Source::Legacy;
    return Source::Empty;
}
bool ConfigStore::Save(const Values& Data) const
{
    // Only a known-good cache can replace the recovery copy.
    Values Previous;
    if (Read(Cache, Previous, true))
    {
        auto Backup = Cache; Backup += L".bak";
        if (!Write(Backup, Previous)) return false;
    }
    return Write(Cache, Data);
}
