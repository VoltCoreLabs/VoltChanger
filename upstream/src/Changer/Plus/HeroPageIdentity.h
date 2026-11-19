#pragma once
#include <windows.h>
#include <string>
namespace HeroPageIdentity
{
    inline std::wstring WideTitle(const std::string& Text)
    {
        const auto Count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
        if (Count <= 0) return {};
        std::wstring Out(Count, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(), static_cast<int>(Text.size()), Out.data(), Count);
        const auto Begin = Out.find_first_not_of(L" \t\r\n\u00a0");
        if (Begin == std::wstring::npos) return {};
        return Out.substr(Begin, Out.find_last_not_of(L" \t\r\n\u00a0") - Begin + 1);
    }
    inline bool SameTitle(const std::string& A, const std::string& B)
    {
        const auto Left = WideTitle(A), Right = WideTitle(B);
        return !Left.empty() && !Right.empty() && CompareStringOrdinal(Left.data(), static_cast<int>(Left.size()),
            Right.data(), static_cast<int>(Right.size()), TRUE) == CSTR_EQUAL;
    }
}
