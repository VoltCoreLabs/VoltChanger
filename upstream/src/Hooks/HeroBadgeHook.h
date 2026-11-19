#pragma once
namespace HeroBadgeHook
{
    bool Install();
    bool Ready();
    bool AllowsLocalVoice(unsigned Hero, unsigned RequiredTier);
    bool NeedsLocalVoice(unsigned Hero, unsigned RequiredTier);
}
