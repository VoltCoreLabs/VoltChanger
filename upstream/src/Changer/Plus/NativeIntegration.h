#pragma once
#include <cstdint>
namespace NativeIntegration
{
    void Update();
    // Opt-in/out of OpenDota content within the natively selected Plus Guide.
    // These functions never select a guide or replace a Workshop guide.
    void ToggleShop(std::intptr_t);
    bool ShopEnabled();
    void AnalyzeBans(std::intptr_t);
}
