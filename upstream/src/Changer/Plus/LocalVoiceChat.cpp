#include "LocalVoiceChat.h"
#include "HeroProgress.h"
#include "HeroProgressPlan.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include "SDK/Panorama/Panel.h"
#include "SDK/Logger/Logger.h"
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

namespace
{
    const std::map<std::string, std::string>& Labels()
    {
        static const auto Values = [] {
            std::map<std::string, std::string> Out;
            HMODULE Module = nullptr; wchar_t Buffer[32768]{};
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&LocalVoiceChat::Post), &Module) || !GetModuleFileNameW(Module, Buffer, 32768)) return Out;
            std::ifstream File(std::filesystem::path(Buffer).parent_path() / L"assistant" / L"voice_lines.json", std::ios::binary);
            if (!File) return Out;
            const std::string Bytes(std::istreambuf_iterator<char>(File), {});
            if (Bytes.size() > 2 * 1024 * 1024) return Out;
            google::protobuf::Struct Document;
            if (!google::protobuf::util::JsonStringToMessage(Bytes, &Document).ok()) return Out;
            for (const auto& [Hero, List] : Document.fields())
            {
                if (List.kind_case() != google::protobuf::Value::kListValue) continue;
                for (const auto& Row : List.list_value().values())
                {
                    if (Row.kind_case() != google::protobuf::Value::kStructValue) continue;
                    const auto& Fields = Row.struct_value().fields();
                    const auto Sound = Fields.find("sound"), Label = Fields.find("label");
                    if (Sound == Fields.end() || Label == Fields.end() || Sound->second.kind_case() != google::protobuf::Value::kStringValue
                        || Label->second.kind_case() != google::protobuf::Value::kStringValue) continue;
                    const auto& Name = Sound->second.string_value(); const auto& Token = Label->second.string_value();
                    if (Name.empty() || Name.size() > 256 || Token.empty() || Token[0] != '#' || Token.size() > 512) continue;
                    auto [It, Added] = Out.emplace(Name, Token);
                    if (!Added && It->second != Token) It->second.clear(); // ambiguous metadata never guesses a phrase
                }
            }
            return Out;
        }();
        return Values;
    }
    std::string Escape(const std::string& Text)
    {
        std::string Out;
        for (const char C : Text)
            switch (C)
            {
            case '&': Out += "&amp;"; break;
            case '<': Out += "&lt;"; break;
            case '>': Out += "&gt;"; break;
            case '"': Out += "&quot;"; break;
            case '\'': Out += "&#39;"; break;
            case '\r': case '\n': case '\t': Out += ' '; break;
            default: if (static_cast<unsigned char>(C) >= 32) Out += C; break;
            }
        return Out;
    }
}
bool LocalVoiceChat::Post(unsigned Message, const std::string& Sound)
{
    auto* Hero = C_DOTAPlayerController::LocalHero();
    if (!Hero || Hero->PlayerID() < 0 || Hero->PlayerID() >= 24) return false;
    const auto& Metadata = Labels(); const auto Found = Metadata.find(Sound);
    if (Found == Metadata.end() || Found->second.empty())
    { LogW("Local voice chat: no unambiguous label for message=%u sound=%s", Message, Sound.c_str()); return false; }
    Panorama::Scope Scope(Panorama::RootKind::Hud); if (!Scope.IsValid()) return false;
    auto Root = Panel::Root(); auto Chat = Root.FindChildTraverse("HudChat"); Root.Release();
    const bool Ready = Chat.IsValid() && Chat.Type() == "DOTAChat"; Chat.Release(); if (!Ready) return false;
    V8::Local LocalizeArgs[] = {Panorama::String(Found->second.c_str())};
    auto Localized = Panorama::Invoke(Panorama::Dollar(), "Localize", 1, LocalizeArgs);
    if (!Localized || V8::TypeOf(Panorama::Isolate(), Localized) != "string") return false;
    const auto Text = V8::ToString(Panorama::Isolate(), Localized);
    if (Text.empty() || Text == Found->second || Text.size() > 2048) return false;
    const auto Caption = std::string("<font color=\"") + HeroProgressPlan::Color(HeroProgress::Level(Hero->HeroId()))
        + "\">" + Escape(Text) + "</font>";
    // Native registered local printf event: (text, PlayerID_t, flags).
    // Native chat owns player naming, history, animation and expiry. This is
    // not DOTASubmitChatText, a chat command, or a client network message.
    V8::Local Args[] = {Panorama::String("DOTAChatMessagePrintf"), Panorama::String(Caption.c_str()),
        Panorama::Number(Hero->PlayerID()), Panorama::Number(0)};
    Panorama::Invoke(Panorama::Dollar(), "DispatchEvent", 4, Args);
    Log("Local voice chat printf: message=%u player=%d label=%s (client only)", Message, Hero->PlayerID(), Found->second.c_str());
    return true;
}
