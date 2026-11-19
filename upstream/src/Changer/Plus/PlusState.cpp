#include "PlusState.h"
#include "ovr_plus.pb.h"
#include "gcsdk_gcmessages.pb.h"

namespace
{
    bool Own(const CMsgSOIDOwner& Owner, std::uint64_t SteamId)
    {
        return SteamId && Owner.type() == 1 && Owner.id() == SteamId;
    }
    bool Apply(std::string& Data, std::uint32_t AccountId, std::uint32_t Now)
    {
        ovr::PlusAccount Message;
        if (!Message.ParseFromString(Data) || Message.account_id() != AccountId)
            return false;
        // Leave existing active membership and every payment-related field intact.
        if (Message.plus_status() == 1 || Message.plus_status() == 2)
            return false;
        Message.set_plus_status(1); // Prepaid state; native client has separate 1/2 paths.
        Message.set_original_start_date(Now);
        Message.set_prepaid_time_start(Now);
        Message.set_prepaid_time_balance(86400u * 365u);
        Data = Message.SerializeAsString();
        return true;
    }
    bool Cache(CMsgSOCacheSubscribed& Message, std::uint64_t SteamId, std::uint32_t Now)
    {
        if (!Own(Message.owner_soid(), SteamId))
            return false;
        CMsgSOCacheSubscribed::SubscribedType* Plus = nullptr;
        bool Found = false, Changed = false;
        for (auto& Type : *Message.mutable_objects())
        {
            if (Type.type_id() != PlusState::SoType)
                continue;
            Plus = &Type;
            for (auto& Data : *Type.mutable_object_data())
            {
                ovr::PlusAccount Record;
                if (!Record.ParseFromString(Data) || Record.account_id() != static_cast<std::uint32_t>(SteamId))
                    continue;
                Found = true;
                Changed |= Apply(Data, static_cast<std::uint32_t>(SteamId), Now);
            }
        }
        if (!Found)
        {
            if (!Plus) { Plus = Message.add_objects(); Plus->set_type_id(PlusState::SoType); }
            Plus->add_object_data(PlusState::LocalAccount(static_cast<std::uint32_t>(SteamId), Now));
            Changed = true;
        }
        return Changed;
    }
}

std::string PlusState::LocalAccount(std::uint32_t AccountId, std::uint32_t Now)
{
    ovr::PlusAccount Record;
    Record.set_account_id(AccountId);
    std::string Data = Record.SerializeAsString();
    Apply(Data, AccountId, Now);
    return Data;
}

bool PlusState::Rewrite(std::uint32_t EMsg, std::string& Body, std::uint64_t SteamId, std::uint32_t Now)
{
    if (!SteamId)
        return false;
    if (EMsg == 4004) // Client welcome may embed the entire shared object cache.
    {
        CMsgClientWelcome Message;
        if (!Message.ParseFromString(Body)) return false;
        bool Changed = false;
        for (auto& Subscribed : *Message.mutable_outofdate_subscribed_caches())
            Changed |= Cache(Subscribed, SteamId, Now);
        if (!Changed) return false;
        Body = Message.SerializeAsString(); return true;
    }
    if (EMsg == 24)
    {
        CMsgSOCacheSubscribed Message;
        if (!Message.ParseFromString(Body) || !Cache(Message, SteamId, Now)) return false;
        Body = Message.SerializeAsString(); return true;
    }
    if (EMsg == 21 || EMsg == 22)
    {
        CMsgSOSingleObject Message;
        if (!Message.ParseFromString(Body) || !Own(Message.owner_soid(), SteamId) || Message.type_id() != SoType ||
            !Apply(*Message.mutable_object_data(), static_cast<std::uint32_t>(SteamId), Now)) return false;
        Body = Message.SerializeAsString(); return true;
    }
    if (EMsg == 26)
    {
        CMsgSOMultipleObjects Message;
        if (!Message.ParseFromString(Body) || !Own(Message.owner_soid(), SteamId)) return false;
        bool Changed = false;
        for (auto& Object : *Message.mutable_objects_added())
            if (Object.type_id() == SoType) Changed |= Apply(*Object.mutable_object_data(), static_cast<std::uint32_t>(SteamId), Now);
        for (auto& Object : *Message.mutable_objects_modified())
            if (Object.type_id() == SoType) Changed |= Apply(*Object.mutable_object_data(), static_cast<std::uint32_t>(SteamId), Now);
        if (!Changed) return false;
        Body = Message.SerializeAsString(); return true;
    }
    return false;
}
