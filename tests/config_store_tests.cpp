#include "SDK/Config/ConfigStore.h"
#include "base_gcmessages.pb.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool Value, const char* Message) { if (!Value) throw std::runtime_error(Message); }
std::string Hex(const std::string& Bytes)
{
    const char* Digits = "0123456789abcdef"; std::string Out;
    for (unsigned char C : Bytes) { Out += Digits[C >> 4]; Out += Digits[C & 15]; }
    return Out + ';';
}
void Write(const std::filesystem::path& Path, const std::string& Text) { std::ofstream F(Path, std::ios::binary); F << Text; }
int main()
{
    const auto TemporaryRoot = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const auto Root = TemporaryRoot / (L"ovr-cache-тест-" + std::to_wstring(GetCurrentProcessId()));
    Check(Root.is_absolute() && Root.parent_path() == TemporaryRoot, "cleanup target must remain inside the temporary test directory");
    try
    {
        std::filesystem::create_directories(Root);
        const auto Cache = Root / L"кэш" / L"inventory.cfg", Legacy = Root / L"ovr_skins.cfg";
        CSOEconItem Item; Item.set_id(0x7000000000002c4bull); Item.set_def_index(11339); Item.set_account_id(7); Item.set_style(2);
        auto* State = Item.add_equipped_state(); State->set_new_class(0); State->set_new_slot(14);
        auto* Extra = Item.add_attribute(); Extra->set_def_index(999); Extra->set_value_bytes("\0\1\xff", 3);
        const std::string Inventory = Hex(Item.SerializeAsString());
        Write(Legacy, "\xef\xbb\xbfskin.items=" + Inventory + "\r\nunusual.items=preserved\r\n");
        ConfigStore First(Cache, Legacy); ConfigStore::Values Data;
        Check(First.Load(Data) == ConfigStore::Source::Legacy && Data.at("skin.items") == Inventory, "first launch must import legacy items and full equipped/style bytes");
        Check(First.Save(Data), "Unicode cache path must save");
        const auto OtherLegacy = Root / L"new-build.cfg"; Write(OtherLegacy, "skin.items=wrong\n");
        ConfigStore Restart(Cache, OtherLegacy); ConfigStore::Values Restored;
        Check(Restart.Load(Restored) == ConfigStore::Source::Cache && Restored == Data, "new DLL directory must retain canonical inventory, ignoring staged legacy");
        auto NewData = Data; NewData["skin.items"] = Inventory + Inventory;
        Check(Restart.Save(NewData), "updated equip snapshot must save");
        Write(Cache, "# OVR persistent inventory v1\nskin.items=truncated");
        Check(Restart.Load(Restored) == ConfigStore::Source::Backup && Restored == Data, "truncated primary must recover previous complete snapshot");
        Check(Restart.Save(Restored), "backup recovery must repair primary without replacing good backup with corruption");
        ConfigStore Fresh(Cache, OtherLegacy);
        Check(Fresh.Load(Restored) == ConfigStore::Source::Cache && Restored == Data, "recovered snapshot must survive another restart");
        HANDLE Lock = CreateFileW(Cache.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(Lock != INVALID_HANDLE_VALUE, "lock fixture");
        const bool Saved = Fresh.Save(NewData); CloseHandle(Lock);
        Check(!Saved && Fresh.Load(Restored) == ConfigStore::Source::Cache && Restored == Data, "failed replacement must retain original cache");
        Check(Fresh.Save({}), "intentional empty inventory must save");
        Check(Fresh.Load(Restored) == ConfigStore::Source::Cache && Restored.empty(), "empty valid cache must not resurrect old legacy items");
        std::filesystem::remove_all(Root);
        std::cout << "Persistent inventory migration, restart, Unicode, backup and atomic-write scenarios passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; std::filesystem::remove_all(Root); return 1; }
}
