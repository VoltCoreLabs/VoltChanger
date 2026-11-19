#include "Changer/Plus/PlusState.h"
#include "Changer/Inventory/InventoryWelcome.h"
#include "base_gcmessages.pb.h"
#include "ovr_plus.pb.h"
#include "gcsdk_gcmessages.pb.h"
#include <iostream>
#include <stdexcept>

constexpr std::uint64_t Owner = 76561197960265728ull + 123;
void Check(bool Value, const char* Message) { if (!Value) throw std::runtime_error(Message); }
int main()
{
    try
    {
        CMsgSOCacheSubscribed Cache;
        Cache.mutable_owner_soid()->set_type(1); Cache.mutable_owner_soid()->set_id(Owner);
        auto* Other = Cache.add_objects(); Other->set_type_id(99); Other->add_object_data("unrelated");
        std::string Data = Cache.SerializeAsString();
        Check(PlusState::Rewrite(24, Data, Owner, 100), "missing plus state not added");
        Check(Cache.ParseFromString(Data) && Cache.objects_size() == 2 && Cache.objects(0).object_data(0) == "unrelated", "unrelated objects changed");
        ovr::PlusAccount Account;
        Check(Account.ParseFromString(Cache.objects(1).object_data(0)) && Account.account_id() == 123 && Account.plus_status() == 1, "wrong plus account state");
        auto Same = Data; Check(!PlusState::Rewrite(24, Data, Owner, 101) && Data == Same, "rewrite not idempotent");
        Check(!PlusState::Rewrite(24, Data, Owner + 1, 101) && Data == Same, "foreign owner changed");
        Check(!PlusState::Rewrite(1059, Data, Owner, 101) && Data == Same, "outgoing request message changed");
        Check(!PlusState::Rewrite(24, Data, 0, 101), "missing local owner accepted");

        Account.Clear(); Account.set_account_id(123); Account.set_plus_status(0);
        std::string Raw = Account.SerializeAsString(); Raw.append("\x9a\x06\x04keep", 7);
        CMsgSOSingleObject Single; *Single.mutable_owner_soid() = Cache.owner_soid(); Single.set_type_id(2012); Single.set_object_data(Raw);
        Data = Single.SerializeAsString(); Check(PlusState::Rewrite(22, Data, Owner, 100), "inactive record not updated");
        Check(Single.ParseFromString(Data) && Single.object_data().find("keep") != std::string::npos, "unknown fields lost");
        Single.set_type_id(1); Data = Single.SerializeAsString(); Same = Data;
        Check(!PlusState::Rewrite(22, Data, Owner, 100) && Data == Same, "econ record changed");

        Account.set_plus_status(2); Account.set_steam_agreement_id(999); Account.set_prepaid_time_balance(777);
        Single.set_type_id(2012); Single.set_object_data(Account.SerializeAsString()); Data = Single.SerializeAsString(); Same = Data;
        Check(!PlusState::Rewrite(22, Data, Owner, 100) && Data == Same, "active subscription changed");

        CMsgClientWelcome Welcome; auto* Nested = Welcome.add_outofdate_subscribed_caches(); *Nested->mutable_owner_soid() = Cache.owner_soid();
        Data = Welcome.SerializeAsString(); Check(PlusState::Rewrite(4004, Data, Owner, 100), "welcome cache not handled");
        Check(Welcome.ParseFromString(Data) && Welcome.outofdate_subscribed_caches(0).objects(0).type_id() == 2012, "welcome state missing");
        auto* Foreign = Welcome.add_outofdate_subscribed_caches(); Foreign->mutable_owner_soid()->set_type(1); Foreign->mutable_owner_soid()->set_id(Owner + 1);
        const auto ForeignBefore = Foreign->SerializeAsString();
        unsigned Scheduled = 0; Data = Welcome.SerializeAsString();
        Check(InventoryWelcome::Rewrite(Data, [&](std::string& CacheBody) {
            CMsgSOCacheSubscribed C; if (!C.ParseFromString(CacheBody) || C.owner_soid().id() != Owner) return false;
            ++Scheduled; return true;
        }), "embedded welcome subscription must schedule local inventory restoration even without an econ collection");
        Check(Welcome.ParseFromString(Data) && Welcome.outofdate_subscribed_caches(1).SerializeAsString() == ForeignBefore,
              "foreign embedded inventory cache must remain unchanged");
        const auto& OwnCache = Welcome.outofdate_subscribed_caches(0);
        Check(Scheduled == 1 && OwnCache.objects_size() == 1 && OwnCache.objects(0).type_id() == 2012,
              "welcome reconstruction must preserve object types without injecting synthetic objects into a native cache rebuild");
        std::string BadWelcome = "\xff\xff"; const auto BadBefore = BadWelcome;
        Check(!InventoryWelcome::Rewrite(BadWelcome, [](std::string&) { return true; }) && BadWelcome == BadBefore,
              "malformed welcome must not mutate or invoke inventory rewriting");
        Data = "\xff\xff"; Same = Data; Check(!PlusState::Rewrite(24, Data, Owner, 100) && Data == Same, "malformed cache mutated");
        std::cout << "All local Plus cache scenarios passed\n";
    }
    catch (const std::exception& Error) { std::cerr << Error.what() << '\n'; return 1; }
}
