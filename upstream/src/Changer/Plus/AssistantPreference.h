#pragma once

namespace AssistantPreference
{
    enum class Status { Pending, AlreadyDisabled, Applied, Unsupported, Failed };

    // Call on RunFrame's UI thread. Only disables dota_plus_use_assistant;
    // returns true once native server-backed recommendations are disabled.
    bool ApplyOnce();
    Status GetStatus();
}
