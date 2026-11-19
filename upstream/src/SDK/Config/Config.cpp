#include "Config.h"
#include "ConfigStore.h"
#include "SDK/Logger/Logger.h"
#include <windows.h>
#include <memory>
#include <mutex>
#include <vector>

namespace
{
    ConfigStore::Values Values;
    std::unique_ptr<ConfigStore> Store;
    std::mutex Mutex;
    std::filesystem::path LegacyPath()
    {
        HMODULE Module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Config::Init), &Module)) return {};
        std::vector<wchar_t> Buffer(32768);
        const auto Length = GetModuleFileNameW(Module, Buffer.data(), static_cast<DWORD>(Buffer.size()));
        if (!Length || Length >= Buffer.size()) return {};
        return std::filesystem::path(Buffer.data()).parent_path() / L"ovr_skins.cfg";
    }
    std::filesystem::path CachePath(const std::filesystem::path& Legacy)
    {
        std::vector<wchar_t> Buffer(32768);
        const auto Length = GetEnvironmentVariableW(L"LOCALAPPDATA", Buffer.data(), static_cast<DWORD>(Buffer.size()));
        if (!Length || Length >= Buffer.size()) return Legacy;
        return std::filesystem::path(Buffer.data()) / L"OVR" / L"cache" / L"inventory.cfg";
    }
}
bool Config::Init()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    const auto Legacy = LegacyPath(); if (Legacy.empty()) return false;
    Store = std::make_unique<ConfigStore>(CachePath(Legacy), Legacy);
    const auto Source = Store->Load(Values);
    const auto Utf8 = Store->Path().u8string();
    Log("Persistent inventory %s: source=%d keys=%zu", reinterpret_cast<const char*>(Utf8.c_str()), static_cast<int>(Source), Values.size());
    if (Source == ConfigStore::Source::Legacy || Source == ConfigStore::Source::Backup)
    {
        if (Store->Save(Values)) Log("Persistent inventory migrated/recovered successfully");
        else LogE("Persistent inventory migration/recovery write failed; loaded items remain available");
    }
    return true;
}
std::string Config::Get(const char* Key)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    const auto Entry = Values.find(Key);
    return Entry != Values.end() ? Entry->second : std::string();
}
void Config::Set(const char* Key, const std::string& Value)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Value.empty()) Values.erase(Key); else Values[Key] = Value;
    if (!Store || !Store->Save(Values)) LogE("Persistent inventory save failed; previous disk snapshot retained");
}
