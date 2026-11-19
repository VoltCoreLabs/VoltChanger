#include "Changer/Plus/HistoryWire.h"
#include "ovr_plus.pb.h"
#include <iostream>
#include <stdexcept>

void Check(bool Value, const char* Message) { if (!Value) throw std::runtime_error(Message); }
int main()
{
    try
    {
        ovr::HistoryResponse Response;
        Response.set_request_id(42);
        auto* M = Response.add_matches();
        M->set_match_id(9091678507ull); M->set_start_time(200); M->set_hero_id(74); M->set_winner(false); M->set_duration(1805);
        M = Response.add_matches();
        M->set_match_id(2); M->set_start_time(100); M->set_hero_id(1); M->set_winner(true);
        M = Response.add_matches();
        M->set_match_id(3); M->set_start_time(300); M->set_hero_id(1); // no final result
        std::vector<BattleData::Match> Out;
        Check(HistoryWire::Decode(Response.SerializeAsString(), 42, Out), "matching response rejected");
        Check(Out.size() == 2 && Out[0].Id == 9091678507ull && !Out[0].Won && Out[1].Won, "real results and 64-bit match IDs must be preserved");
        Check(Out[0].Duration == 1805 && Out[1].Duration == UINT32_MAX && Out[0].Kills == -1, "missing stats must stay unknown");
        Check(!HistoryWire::Decode(Response.SerializeAsString(), 43, Out) && Out.size() == 2, "unrelated history must not replace own response");
        Check(!HistoryWire::Decode("\xff", 42, Out) && Out.size() == 2, "malformed reply changed output");
        Response.clear_matches();
        Check(HistoryWire::Decode(Response.SerializeAsString(), 42, Out) && Out.empty(), "valid empty response not distinguished from failure");
        Response.clear_request_id();
        Check(!HistoryWire::Decode(Response.SerializeAsString(), 42, Out), "response without correlation must not be accepted");
        std::cout << "History wire tests passed\n";
    }
    catch (const std::exception& E) { std::cerr << E.what() << '\n'; return 1; }
}
