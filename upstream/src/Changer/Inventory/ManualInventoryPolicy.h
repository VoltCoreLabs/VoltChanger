#pragma once
namespace ManualInventoryPolicy
{
    constexpr bool Archive(bool Listed, bool Canonical, bool Protected, bool Equipped, unsigned Style, bool ExtraAttributes)
    { return Listed && Canonical && !Protected && !Equipped && (Style == 0 || Style == 255) && !ExtraAttributes; }
}
