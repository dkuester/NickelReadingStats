// Host stand-in for NickelHook's nh_log() (defined in NickelHook/nh.c, which
// the host tests do not link). Include after ../src/readingstats.cc.
#pragma once

#include <cstdarg>
#include <cstdio>

extern "C" void nh_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::printf("[nh_log] ");
    std::vprintf(fmt, args);
    std::printf("\n");
    va_end(args);
}
