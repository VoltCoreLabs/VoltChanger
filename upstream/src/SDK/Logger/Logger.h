#pragma once

namespace Logger
{
    bool Init();
    void Write(const char* Prefix, const char* Format, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(gnu_printf, 2, 3)))
#endif
        ;
}

#define Log(...) Logger::Write("[+]", __VA_ARGS__)
#define LogW(...) Logger::Write("[!]", __VA_ARGS__)
#define LogE(...) Logger::Write("[-]", __VA_ARGS__)
