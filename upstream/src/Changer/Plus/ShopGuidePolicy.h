#pragma once
namespace ShopGuidePolicy
{
    // PlusGuide is native selection state on DOTAHUDShop, not a style we set.
    // Native parent visibility independently keeps this content out of Workshop guides.
    constexpr bool UseOpenDota(bool Enabled, bool HasHero, bool NativePlusGuide)
    {
        return Enabled && HasHero && NativePlusGuide;
    }
}
