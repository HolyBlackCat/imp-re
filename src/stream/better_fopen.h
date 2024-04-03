#pragma once

#include <cstdio>

#ifdef __MINGW32__
#include <filesystem>
#include "program/compiler.h"
#include "program/platform.h"
#endif


namespace Stream
{
    // Like regular fopen, but can handle unicode paths on MinGW.
    [[nodiscard]] inline FILE *better_fopen(const char *name, const char *mode)
    {
        #ifndef __MINGW32__
        return std::fopen(name, mode);
        #else
        constexpr int max_mode_len = 3;
        wchar_t wide_mode[max_mode_len + 1]{};
        for (int i = 0; i < max_mode_len; i++)
        {
            wide_mode[i] = mode[i];
            if (mode[i] == '\0')
                break;
        }
        IMP_PLATFORM_IF(gcc_clang)(IMP_DIAGNOSTICS_PUSH IMP_DIAGNOSTICS_IGNORE("-Wdeprecated-declarations"))
        return _wfopen(std::filesystem::u8path(name).c_str(), wide_mode); // In C++20 `u8path` will be deprecated in favor of a new constructor.
        IMP_PLATFORM_IF(gcc_clang)(IMP_DIAGNOSTICS_POP)
        #endif
    }
}
