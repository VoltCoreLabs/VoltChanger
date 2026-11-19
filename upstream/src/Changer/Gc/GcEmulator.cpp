#include "GcEmulator.h"
#include "Changer/Inventory/FakeItems.h"
#include "Changer/Inventory/ItemAcknowledgement.h"

#include "Hooks/GcRecvHook.h"
#include "SDK/SteamAPI/ISteamUser.h"

#include "gcsdk_gcmessages.pb.h"

#include <cstring>
#include <string>
#include <algorithm>

namespace
{
    constexpr std::uint32_t EMsgSoUpdateMultiple = 26;
    constexpr std::uint32_t ProtoFlag = 0x80000000u;
    constexpr int SoTypeEconItem = 1;
    constexpr std::uint32_t SoOwnerTypeUser = 1;

    std::string LocalItemBytes(const CSOEconItem& Item)
    {
        if (!FakeItems::IsFake(Item.id()) || !ItemAcknowledgement::Pending(Item.inventory())) return Item.SerializeAsString();
        auto Acknowledged = Item;
        Acknowledged.set_inventory(ItemAcknowledgement::Position(Item.inventory(), Item.def_index()));
        return Acknowledged.SerializeAsString();
    }

    void Push(std::uint32_t EMsg, const CMsgProtoBufHeader& Header, const std::string& Body)
    {
        std::string HeaderBytes = Header.SerializeAsString();
        std::vector<std::uint8_t> Out(8 + HeaderBytes.size() + Body.size());
        std::uint32_t Type = EMsg | ProtoFlag;
        std::uint32_t HeaderLen = static_cast<std::uint32_t>(HeaderBytes.size());
        std::memcpy(Out.data(), &Type, 4);
        std::memcpy(Out.data() + 4, &HeaderLen, 4);
        std::memcpy(Out.data() + 8, HeaderBytes.data(), HeaderBytes.size());
        std::memcpy(Out.data() + 8 + HeaderBytes.size(), Body.data(), Body.size());
        GcRecvHook::PushFake(std::move(Out));
        GcRecvHook::Pump();
    }
}

bool GcEmulator::Parse(const void* Data, std::uint32_t Size, CMsgProtoBufHeader& Header, google::protobuf::MessageLite& Body)
{
    auto* Bytes = static_cast<const std::uint8_t*>(Data);
    if (!Bytes || Size < 8)
        return false;
    std::uint32_t HeaderLen = 0;
    std::memcpy(&HeaderLen, Bytes + 4, 4);
    if (8 + HeaderLen > Size)
        return false;
    return Header.ParseFromArray(Bytes + 8, static_cast<int>(HeaderLen)) &&
           Body.ParseFromArray(Bytes + 8 + HeaderLen, static_cast<int>(Size - 8 - HeaderLen));
}

void GcEmulator::Respond(std::uint32_t EMsg, const CMsgProtoBufHeader& Request, const google::protobuf::MessageLite& Body)
{
    CMsgProtoBufHeader Header;
    Header.set_job_id_target(Request.job_id_source());
    Push(EMsg, Header, Body.SerializeAsString());
}

void GcEmulator::SendObject(int Type, const std::string& Body)
{
    auto Owner = GcRecvHook::Owner();
    if (!Owner.Valid) { auto* User = ISteamUser::Get(); Owner.Id = User ? User->GetSteamID() : 0; }
    if (!Owner.Id) return;
    CMsgSOSingleObject Message;
    Message.mutable_owner_soid()->set_type(SoOwnerTypeUser);
    Message.mutable_owner_soid()->set_id(Owner.Id);
    Message.set_version(Owner.Version);
    Message.set_type_id(Type);
    Message.set_object_data(Body);
    Push(21, CMsgProtoBufHeader(), Message.SerializeAsString());
}

void GcEmulator::SendItems(const std::vector<CSOEconItem>& Added, const std::vector<CSOEconItem>& Modified)
{
    if (Added.empty() && Modified.empty())
        return;
    constexpr std::size_t Batch = 256;
    if (Added.size() + Modified.size() > Batch)
    {
        for (std::size_t I = 0; I < Added.size(); I += Batch)
            SendItems(std::vector<CSOEconItem>(Added.begin() + I, Added.begin() + (std::min)(Added.size(), I + Batch)), {});
        for (std::size_t I = 0; I < Modified.size(); I += Batch)
            SendItems({}, std::vector<CSOEconItem>(Modified.begin() + I, Modified.begin() + (std::min)(Modified.size(), I + Batch)));
        return;
    }

    GcRecvHook::SoOwner Owner = GcRecvHook::Owner();
    if (!Owner.Valid)
    {
        ISteamUser* User = ISteamUser::Get();
        Owner.Id = User ? User->GetSteamID() : 0;
    }

    CMsgSOMultipleObjects Update;
    Update.mutable_owner_soid()->set_type(SoOwnerTypeUser);
    Update.mutable_owner_soid()->set_id(Owner.Id);
    Update.set_version(Owner.Version);
    for (const CSOEconItem& Item : Added)
    {
        auto* Object = Update.add_objects_added();
        Object->set_type_id(SoTypeEconItem);
        Object->set_object_data(LocalItemBytes(Item));
    }
    for (const CSOEconItem& Item : Modified)
    {
        auto* Object = Update.add_objects_modified();
        Object->set_type_id(SoTypeEconItem);
        Object->set_object_data(LocalItemBytes(Item));
    }
    Push(EMsgSoUpdateMultiple, CMsgProtoBufHeader(), Update.SerializeAsString());
}
