#include "CosmeticCatalog.h"
#include "FakeItems.h"
#include "ManualInventoryPolicy.h"
#include "SDK/Config/Config.h"
#include "SDK/Logger/Logger.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <mutex>
namespace
{
    int Hex(char C) { if (C >= '0' && C <= '9') return C - '0'; if (C >= 'a' && C <= 'f') return C - 'a' + 10; if (C >= 'A' && C <= 'F') return C - 'A' + 10; return -1; }
    void ReadIds(const std::string& Serialized, std::set<std::uint64_t>& Ids)
    {
        std::istringstream Rows(Serialized); std::string Row;
        while (std::getline(Rows, Row, ';'))
        {
            if (Row.empty() || Row.size() % 2) continue;
            std::string Bytes; bool Valid = true;
            for (std::size_t I = 0; I < Row.size(); I += 2)
            {
                const auto A = Hex(Row[I]), B = Hex(Row[I + 1]);
                if (A < 0 || B < 0) { Valid = false; break; }
                Bytes.push_back(static_cast<char>((A << 4) | B));
            }
            CSOEconItem Item; if (Valid && Item.ParseFromString(Bytes) && Item.id()) Ids.insert(Item.id());
        }
    }
    void Archive(const CSOEconItem& Item, std::string& Out)
    {
        constexpr const char* Digits = "0123456789abcdef";
        for (unsigned char C : Item.SerializeAsString()) { Out.push_back(Digits[C >> 4]); Out.push_back(Digits[C & 15]); }
        Out.push_back(';');
    }
}
void CosmeticCatalog::UseManualInventory()
{
    static std::mutex Mutex; static bool Done = false;
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Done) return;
    if (Config::Get("catalog.manual_mode.v1") == "1") { Done = true; return; }
    HMODULE Module = nullptr; wchar_t Path[32768]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&CosmeticCatalog::UseManualInventory), &Module) || !GetModuleFileNameW(Module, Path, 32768)) return;
    const auto Directory = std::filesystem::path(Path).parent_path();
    std::ifstream Manifest(Directory / L"assistant" / L"cosmetic_catalog.txt");
    if (!Manifest) { LogW("Manual inventory migration: catalog manifest missing; saved items retained"); return; }
    std::set<unsigned> AutoDefinitions; std::string Line;
    while (std::getline(Manifest, Line))
    {
        std::istringstream Row(Line); unsigned Id = 0; char Kind = 0;
        if (Row >> Id >> Kind && (Kind == 'G' || Kind == 'H')) AutoDefinitions.insert(Id);
    }
    if (AutoDefinitions.empty()) return;
    std::set<std::uint64_t> Protected;
    for (const auto* Name : {L"ovr_skins.cfg", L"ovr_skins.cfg.previous"})
    {
        std::ifstream Legacy(Directory / Name);
        while (std::getline(Legacy, Line)) if (Line.starts_with("skin.items="))
        { if (!Line.empty() && Line.back() == '\r') Line.pop_back(); ReadIds(Line.substr(11), Protected); }
    }
    std::vector<std::uint64_t> Removed; std::string SavedArchive;
    for (const auto& Item : FakeItems::All())
    {
        bool Extra = false;
        for (const auto& A : Item.attribute()) if (A.def_index() != 400) Extra = true;
        if (!ManualInventoryPolicy::Archive(AutoDefinitions.count(Item.def_index()) != 0,
            Item.id() == (FakeItems::IdBase | Item.def_index()), Protected.count(Item.id()) != 0,
            Item.equipped_state_size() != 0, Item.style(), Extra)) continue;
        Removed.push_back(Item.id()); Archive(Item, SavedArchive);
    }
    // Retain full removed records for recovery outside active skin.items.
    if (!SavedArchive.empty()) Config::Set("skin.catalog_archive.v1", SavedArchive);
    FakeItems::Remove(Removed);
    Config::Set("catalog.manual_mode.v1", "1"); Done = true;
    Log("Manual inventory enabled: archived %zu untouched catalog items, legacy protected=%zu; equipped/customized items retained", Removed.size(), Protected.size());
}
