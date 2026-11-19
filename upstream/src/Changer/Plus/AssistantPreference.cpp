#include "AssistantPreference.h"
#include "SDK/Logger/Logger.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
    AssistantPreference::Status Current = AssistantPreference::Status::Pending;
    constexpr std::uintptr_t RegistrationRva = 0x444bbe;
    constexpr std::uintptr_t ReferenceRva = 0x6408718;
    constexpr std::uintptr_t CallbackRva = 0x2ac14c0;
    constexpr char Name[] = "dota_plus_use_assistant";
    // client.dll SHA256 8bec51d15c8c4159bc515617b122d28ca69f711201d20a571a4e3367c2cfdef5.
    // PE identity plus exact registration/callback bytes and live name are all required.
    constexpr unsigned char Registration[] = {
        0x48,0x8d,0x15,0x8b,0x44,0x4a,0x04,0x48,0x8d,0x0d,0x4c,0x3b,0xfc,0x05,
        0xe8,0xbf,0x23,0x69,0x03
    };
    constexpr unsigned char Callback[] = {
        0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x1d,0xe3,0x22,0xb0,0x03,0x48,0x85,0xdb,
        0x74,0x55,0x48,0x8b,0x03,0xb9,0x20,0x00,0x00,0x00,0x48,0x89,0x7c,0x24,0x38,0x48,
        0x8b,0xb8,0x78,0x01,0x00,0x00,0xe8,0x45,0x25,0xf3,0xfe,0x48,0x85,0xc0,0x74,0x23,
        0x0f,0xb7,0x0d,0x61,0xb9,0x0d,0x03,0x66,0x89,0x48,0x08,0x48,0xb9,0x00,0x00,0x00,
        0x00,0xff,0xff,0xff,0xff,0x48,0x89,0x48,0x0c,0x48,0x8d,0x0d,0xc8,0xba,0x1a,0x01,
        0x48,0x89,0x08,0x48,0x8d,0x54,0x24,0x30,0x48,0x89,0x44,0x24,0x30,0x48,0x8b,0xcb,
        0xff,0xd7,0x48,0x8b,0x7c,0x24,0x38,0x48,0x83,0xc4,0x20,0x5b,0xc3
    };

    bool Accessible(std::uintptr_t Address, std::size_t Size, bool Write = false)
    {
        if (!Address || !Size || Size > std::numeric_limits<std::uintptr_t>::max() - Address)
            return false;
        const auto End = Address + Size;
        while (Address < End)
        {
            MEMORY_BASIC_INFORMATION Region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(Address), &Region, sizeof(Region)) ||
                Region.State != MEM_COMMIT || (Region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
                return false;
            const DWORD Access = Region.Protect & 0xff;
            const bool Readable = Access == PAGE_READONLY || Access == PAGE_READWRITE || Access == PAGE_WRITECOPY ||
                                  Access == PAGE_EXECUTE_READ || Access == PAGE_EXECUTE_READWRITE || Access == PAGE_EXECUTE_WRITECOPY;
            const bool Writable = Access == PAGE_READWRITE || Access == PAGE_WRITECOPY ||
                                  Access == PAGE_EXECUTE_READWRITE || Access == PAGE_EXECUTE_WRITECOPY;
            if (!Readable || (Write && !Writable))
                return false;
            const auto Base = reinterpret_cast<std::uintptr_t>(Region.BaseAddress);
            if (Region.RegionSize > std::numeric_limits<std::uintptr_t>::max() - Base)
                return false;
            const auto Next = Base + Region.RegionSize;
            if (Next <= Address)
                return false;
            Address = Next;
        }
        return true;
    }

    bool Copy(void* Out, std::uintptr_t Address, std::size_t Size)
    {
        if (!Accessible(Address, Size))
            return false;
        __try { std::memcpy(Out, reinterpret_cast<const void*>(Address), Size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool Matches(std::uintptr_t Address, const void* Expected, std::size_t Size)
    {
        if (!Accessible(Address, Size))
            return false;
        __try { return std::memcmp(reinterpret_cast<const void*>(Address), Expected, Size) == 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ChangeAndNotify(std::uintptr_t Value, std::uintptr_t Callback)
    {
        // The verified callback has no arguments and only dispatches the normal UI event.
        __try
        {
            *reinterpret_cast<volatile unsigned char*>(Value) = 0;
            reinterpret_cast<void (*)()>(Callback)();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
}

bool AssistantPreference::ApplyOnce()
{
    if (Current != Status::Pending)
        return Current == Status::Applied || Current == Status::AlreadyDisabled;
    const auto Base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    if (!Base)
        return false;
    IMAGE_DOS_HEADER Dos{};
    IMAGE_NT_HEADERS64 Nt{};
    if (!Copy(&Dos, Base, sizeof(Dos)) || Dos.e_magic != IMAGE_DOS_SIGNATURE || Dos.e_lfanew <= 0 || Dos.e_lfanew > 0x1000 ||
        !Copy(&Nt, Base + Dos.e_lfanew, sizeof(Nt)) || Nt.Signature != IMAGE_NT_SIGNATURE ||
        Nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || Nt.FileHeader.TimeDateStamp != 0x6af6615f ||
        Nt.OptionalHeader.SizeOfImage != 0x6e3b000 ||
        !Matches(Base + RegistrationRva, Registration, sizeof(Registration)) ||
        !Matches(Base + CallbackRva, Callback, sizeof(Callback)))
    {
        Current = Status::Unsupported;
        LogW("Assistant preference: client build/signature unsupported; native setting unchanged");
        return false;
    }
    std::int32_t NameDisplacement = 0, ReferenceDisplacement = 0;
    if (!Copy(&NameDisplacement, Base + RegistrationRva + 3, sizeof(NameDisplacement)) ||
        !Copy(&ReferenceDisplacement, Base + RegistrationRva + 10, sizeof(ReferenceDisplacement)) ||
        Base + RegistrationRva + 14 + ReferenceDisplacement != Base + ReferenceRva ||
        !Matches(Base + RegistrationRva + 7 + NameDisplacement, Name, sizeof(Name)))
    {
        Current = Status::Unsupported;
        LogW("Assistant preference: convar registration mismatch; native setting unchanged");
        return false;
    }
    std::uintptr_t Data = 0, DataName = 0;
    if (!Copy(&Data, Base + ReferenceRva + 8, sizeof(Data)) || !Data)
        return false; // Convar registration may not be ready yet.
    if (!Copy(&DataName, Data, sizeof(DataName)) || !Matches(DataName, Name, sizeof(Name)) ||
        !Accessible(Data + 0x58, 1, true))
    {
        Current = Status::Unsupported;
        LogW("Assistant preference: convar data layout mismatch; native setting unchanged");
        return false;
    }
    unsigned char Value = 0;
    if (!Copy(&Value, Data + 0x58, sizeof(Value)) || Value > 1)
    {
        Current = Status::Unsupported;
        LogW("Assistant preference: unexpected boolean value; native setting unchanged");
        return false;
    }
    if (!Value)
    {
        Current = Status::AlreadyDisabled;
        Log("Assistant preference: native dota_plus_use_assistant already disabled");
        return true;
    }
    if (!ChangeAndNotify(Data + 0x58, Base + CallbackRva))
    {
        Current = Status::Failed;
        LogE("Assistant preference: native change notification failed");
        return false;
    }
    Current = Status::Applied;
    Log("Assistant preference: native dota_plus_use_assistant disabled with normal UI notification");
    return true;
}

AssistantPreference::Status AssistantPreference::GetStatus() { return Current; }
