#pragma once
#include <string>
namespace LocalVoiceChat
{
    // Adds a client-only native chat line; never submits chat input/network text.
    bool Post(unsigned Message, const std::string& Sound);
}
