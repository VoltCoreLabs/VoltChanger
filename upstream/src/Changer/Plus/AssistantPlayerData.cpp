#include "AssistantPlayerData.h"
#include "Misc/Offsets.h"
#include "SDK/EntitySystem/CGameEntitySystem.h"
#include "SDK/EntitySystem/EntityUtils/C_BaseEntity.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include "SDK/SchemaSystem/SchemaOffset.h"
#include "SDK/SchemaSystem/CSchemaSystem.h"
#include "SDK/SteamAPI/ISteamUser.h"
#include "SDK/Logger/Logger.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    struct Layout
    {
        bool Checked = false;
        bool Valid = false;
        std::uint32_t Vector = 0;
        std::uint32_t SteamId = 0;
        std::uint32_t NetWorth = 0;
        std::uint32_t Damage = 0;
        bool DamageVerified = false;
        std::size_t Stride = 0;
    };
    Layout Fields;
    std::vector<CHandle> Candidates;
    ULONGLONG LastScan = 0;

    bool Copy(void* Out, std::uintptr_t Address, std::size_t Size)
    {
        if (!Address || !Size || Size > std::numeric_limits<std::uintptr_t>::max() - Address)
            return false;
        const auto End = Address + Size;
        auto Cursor = Address;
        while (Cursor < End)
        {
            MEMORY_BASIC_INFORMATION Region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(Cursor), &Region, sizeof(Region)) ||
                Region.State != MEM_COMMIT || (Region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
                return false;
            const DWORD Access = Region.Protect & 0xff;
            if (Access != PAGE_READONLY && Access != PAGE_READWRITE && Access != PAGE_WRITECOPY &&
                Access != PAGE_EXECUTE_READ && Access != PAGE_EXECUTE_READWRITE && Access != PAGE_EXECUTE_WRITECOPY)
                return false;
            const auto Base = reinterpret_cast<std::uintptr_t>(Region.BaseAddress);
            if (Region.RegionSize > std::numeric_limits<std::uintptr_t>::max() - Base)
                return false;
            const auto Next = Base + Region.RegionSize;
            if (Next <= Cursor)
                return false;
            Cursor = Next;
        }
        __try { std::memcpy(Out, reinterpret_cast<const void*>(Address), Size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool FieldType(const CSchemaClassBinding* Binding, const char* Name, const char* Type)
    {
        if (!Binding)
            return false;
        const auto Count = Binding->FieldCount();
        if (Count < 0 || Count > 2048)
            return false;
        for (int Index = 0; Index < Count; ++Index)
        {
            const auto* Field = Binding->Field(Index);
            if (!Field || std::strcmp(Field->Name(), Name) != 0)
                continue;
            const auto* Actual = Field->Type();
            return Actual && std::strcmp(Actual->Name(), Type) == 0;
        }
        return false;
    }

    bool Bytes(std::uintptr_t Address, const unsigned char* Expected, std::size_t Size)
    {
        unsigned char Actual[64]{};
        return Size <= sizeof(Actual) && Copy(Actual, Address, Size) && std::memcmp(Actual, Expected, Size) == 0;
    }

    bool NativeLayoutVerified()
    {
        const auto Base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
        IMAGE_DOS_HEADER Dos{}; IMAGE_NT_HEADERS64 Nt{};
        if (!Base || !Copy(&Dos, Base, sizeof(Dos)) || Dos.e_magic != IMAGE_DOS_SIGNATURE || Dos.e_lfanew <= 0 || Dos.e_lfanew > 0x1000 ||
            !Copy(&Nt, Base + Dos.e_lfanew, sizeof(Nt)) || Nt.Signature != IMAGE_NT_SIGNATURE ||
            Nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || Nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            Nt.FileHeader.TimeDateStamp != 0x6af6615f || Nt.OptionalHeader.SizeOfImage != 0x6e3b000)
            return false;
        // The native player getter checks count at entity+0x5f0, scales by
        // DataTeamPlayer_t::Size(0x12f0), then adds the data pointer at +0x5f8.
        constexpr unsigned char Count[] = {0x3b,0x98,0xf0,0x05,0x00,0x00};
        constexpr unsigned char Storage[] = {0x48,0x69,0xc3,0xf0,0x12,0x00,0x00,0x48,0x03,0x82,0xf8,0x05,0x00,0x00};
        return Bytes(Base + 0x1f93043, Count, sizeof(Count)) && Bytes(Base + 0x1f93056, Storage, sizeof(Storage));
    }

    bool DamageMappingVerified()
    {
        const auto Base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
        // Installed client SHA256 8bec51d15c8c4159bc515617b122d28ca69f711201d20a571a4e3367c2cfdef5:
        // accessor 1B45360 indexes m_flDamageByTypeReceivedPostReduction at +0xf5c using R8d.
        // Native consumer 2219957/2219981/22199ae passes indices 0/1/2 respectively,
        // then writes the explicit physical/magical/pure keys at 4CAE8C0/4CAE8E8/4CAE910.
        // This is a code-and-label corroboration, not an inferred enum ordering.
        constexpr unsigned char Read[] = {0xf3,0x0f,0x10,0x84,0x98,0x5c,0x0f,0x00,0x00};
        constexpr unsigned char Physical[] = {0x45,0x33,0xc0,0x8b,0xd7,0xe8,0xff,0xb9,0x92,0xff};
        constexpr unsigned char Magical[] = {0x41,0xb8,0x01,0x00,0x00,0x00,0x8b,0xd7,0xe8,0xd2,0xb9,0x92,0xff};
        constexpr unsigned char Pure[] = {0x41,0xb8,0x02,0x00,0x00,0x00,0x8b,0xd7,0xe8,0xa5,0xb9,0x92,0xff};
        constexpr unsigned char PhysicalKey[] = {0x48,0x8d,0x15,0x4e,0x4f,0xa9,0x02};
        constexpr unsigned char MagicalKey[] = {0x48,0x8d,0x15,0x49,0x4f,0xa9,0x02};
        constexpr unsigned char PureKey[] = {0x48,0x8d,0x15,0x44,0x4f,0xa9,0x02};
        char Key0[sizeof("damage_received_post_reduction_physical")]{},
             Key1[sizeof("damage_received_post_reduction_magical")]{},
             Key2[sizeof("damage_received_post_reduction_pure")]{};
        return Bytes(Base + 0x1b4537c, Read, sizeof(Read)) &&
            Bytes(Base + 0x2219957, Physical, sizeof(Physical)) &&
            Bytes(Base + 0x2219981, Magical, sizeof(Magical)) &&
            Bytes(Base + 0x22199ae, Pure, sizeof(Pure)) &&
            Bytes(Base + 0x221996b, PhysicalKey, sizeof(PhysicalKey)) &&
            Bytes(Base + 0x2219998, MagicalKey, sizeof(MagicalKey)) &&
            Bytes(Base + 0x22199c5, PureKey, sizeof(PureKey)) &&
            Copy(Key0, Base + 0x4cae8c0, sizeof(Key0)) && std::strcmp(Key0, "damage_received_post_reduction_physical") == 0 &&
            Copy(Key1, Base + 0x4cae8e8, sizeof(Key1)) && std::strcmp(Key1, "damage_received_post_reduction_magical") == 0 &&
            Copy(Key2, Base + 0x4cae910, sizeof(Key2)) && std::strcmp(Key2, "damage_received_post_reduction_pure") == 0;
    }

    bool Prepare()
    {
        if (Fields.Checked)
            return Fields.Valid;
        auto* Schema = SchemaOffset::Instance();
        auto* Player = Schema->GetClassBinding("DataTeamPlayer_t");
        auto* Team = Schema->GetClassBinding("C_DOTA_DataNonSpectator");
        if (!Player || !Team)
            return false;
        Fields.Checked = true;
        if (!Schema->HasField("C_DOTA_DataNonSpectator", "m_vecDataTeam") ||
            !Schema->HasField("DataTeamPlayer_t", "m_iPlayerSteamID") ||
            !Schema->HasField("DataTeamPlayer_t", "m_iNetWorth") ||
            !FieldType(Team, "m_vecDataTeam", "C_UtlVectorEmbeddedNetworkVar< DataTeamPlayer_t >") ||
            !FieldType(Player, "m_iPlayerSteamID", "uint64") || !FieldType(Player, "m_iNetWorth", "int32"))
        {
            LogW("Assistant player data: unsupported team player schema; values unavailable");
            return false;
        }
        const auto Size = Player->Size();
        Fields.Vector = Schema->GetOffset("C_DOTA_DataNonSpectator", "m_vecDataTeam");
        Fields.SteamId = Schema->GetOffset("DataTeamPlayer_t", "m_iPlayerSteamID");
        Fields.NetWorth = Schema->GetOffset("DataTeamPlayer_t", "m_iNetWorth");
        if (Size <= 0 || Size > 0x10000 || Team->Size() <= 0 || Fields.Vector + 16 > static_cast<unsigned>(Team->Size()) ||
            Fields.SteamId + sizeof(std::uint64_t) > static_cast<unsigned>(Size) ||
            Fields.NetWorth + sizeof(std::int32_t) > static_cast<unsigned>(Size))
        {
            LogW("Assistant player data: invalid schema sizes; values unavailable");
            return false;
        }
        Fields.Stride = static_cast<std::size_t>(Size);
        if (Fields.Vector != 0x5f0 || Fields.Stride != 0x12f0 || !NativeLayoutVerified())
        {
            LogW("Assistant player data: team vector native layout unsupported; values unavailable");
            return false;
        }
        Fields.Valid = true;
        const bool DamageField = Schema->HasField("DataTeamPlayer_t", "m_flDamageByTypeReceivedPostReduction") &&
            FieldType(Player, "m_flDamageByTypeReceivedPostReduction", "float32[3]");
        if (DamageField)
        {
            Fields.Damage = Schema->GetOffset("DataTeamPlayer_t", "m_flDamageByTypeReceivedPostReduction");
            Fields.DamageVerified = Fields.Damage == 0xf5c && Fields.Damage + sizeof(float) * 3 <= Fields.Stride && DamageMappingVerified();
        }
        Log("Assistant player data: own networth schema ready; physical/magical/pure damage mapping verified %d", Fields.DamageVerified);
        return true;
    }

    C_BaseEntity* GetEntity(CGameEntitySystem* System, int Index)
    {
        __try { return System->Entity(Index); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    C_BaseEntity* GetEntity(CGameEntitySystem* System, CHandle Handle)
    {
        __try { return System->Entity(Handle); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    bool TeamEntity(C_BaseEntity* Entity, CHandle& Handle)
    {
        const char* Name = nullptr;
        __try { Name = Entity->DesignerName(); Handle = Entity->Handle(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        char Text[32]{};
        if (!Name || !Handle.IsValid() || !Copy(Text, reinterpret_cast<std::uintptr_t>(Name), sizeof(Text)))
            return false;
        Text[sizeof(Text) - 1] = 0;
        return std::strcmp(Text, "dota_data_radiant") == 0 || std::strcmp(Text, "dota_data_dire") == 0;
    }

    bool LocalIdentity(int LocalPlayerId, CGameEntitySystem*& System, std::uint64_t& SteamId)
    {
        __try
        {
            auto* Hero = C_DOTAPlayerController::LocalHero();
            auto* User = ISteamUser::Get();
            System = CGameEntitySystem::Get();
            if (!Hero || !User || !System || Hero->PlayerID() != LocalPlayerId)
                return false;
            SteamId = User->GetSteamID();
            return SteamId != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ReadRecord(C_BaseEntity* Entity, std::uint64_t SteamId, AssistantPlayerData::Snapshot& Out)
    {
        const auto Vector = reinterpret_cast<std::uintptr_t>(Entity) + Fields.Vector;
        std::int32_t Count = 0;
        std::uintptr_t Data = 0;
        // Native getter verifies the vector header and stride; the exact current
        // user's SteamID establishes the record, with no guessed team slot.
        if (!Copy(&Count, Vector + Offsets::CUtlVector::Size, sizeof(Count)) || Count <= 0 || Count > 64 ||
            !Copy(&Data, Vector + Offsets::CUtlVector::Data, sizeof(Data)) || !Data ||
            static_cast<std::size_t>(Count) * Fields.Stride > std::numeric_limits<std::uintptr_t>::max() - Data)
            return false;
        std::uintptr_t Match = 0;
        for (int Index = 0; Index < Count; ++Index)
        {
            const auto Record = Data + static_cast<std::size_t>(Index) * Fields.Stride;
            std::uint64_t Id = 0;
            if (!Copy(&Id, Record + Fields.SteamId, sizeof(Id)))
                return false;
            if (Id != SteamId)
                continue;
            if (Match)
                return false; // Duplicate identities do not establish a unique current record.
            Match = Record;
        }
        if (!Match)
            return false;
        std::int32_t NetWorth = 0;
        if (!Copy(&NetWorth, Match + Fields.NetWorth, sizeof(NetWorth)) || NetWorth < 0 || NetWorth > 100000000)
            return false;
        Out.NetWorth = NetWorth;
        Out.NetWorthAvailable = true;
        if (Fields.DamageVerified)
        {
            float Damage[3]{};
            if (Copy(Damage, Match + Fields.Damage, sizeof(Damage)) &&
                std::isfinite(Damage[0]) && Damage[0] >= 0 && Damage[0] <= 1.0e12f &&
                std::isfinite(Damage[1]) && Damage[1] >= 0 && Damage[1] <= 1.0e12f &&
                std::isfinite(Damage[2]) && Damage[2] >= 0 && Damage[2] <= 1.0e12f)
            {
                Out.Physical = Damage[0]; Out.Magical = Damage[1]; Out.Pure = Damage[2];
                Out.DamageAvailable = true;
            }
        }
        return true;
    }
}

AssistantPlayerData::Snapshot AssistantPlayerData::Read(int LocalPlayerId)
{
    Snapshot Out;
    if (LocalPlayerId < 0 || LocalPlayerId > 63 || !Prepare())
        return Out;
    CGameEntitySystem* System = nullptr;
    std::uint64_t SteamId = 0;
    if (!LocalIdentity(LocalPlayerId, System, SteamId))
        return Out;
    for (const auto& Handle : Candidates)
    {
        auto* Entity = GetEntity(System, Handle);
        CHandle Verified;
        if (Entity && TeamEntity(Entity, Verified) && ReadRecord(Entity, SteamId, Out))
            return Out;
    }
    const auto Now = GetTickCount64();
    if (LastScan && Now - LastScan < 1000)
        return Out;
    LastScan = Now;
    Candidates.clear();
    for (int Index = 0; Index < CGameEntitySystem::MaxEntities; ++Index)
    {
        auto* Entity = GetEntity(System, Index);
        CHandle Handle;
        if (!Entity || !TeamEntity(Entity, Handle))
            continue;
        Candidates.push_back(Handle);
        if (ReadRecord(Entity, SteamId, Out))
            return Out;
    }
    return Out;
}
