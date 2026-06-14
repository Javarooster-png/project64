#pragma once

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <strings.h>

#ifndef _WIN32
#ifndef __debugbreak
#define __debugbreak() __builtin_trap()
#endif

static inline void * _aligned_malloc(size_t size, size_t alignment)
{
    void * ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0)
    {
        return nullptr;
    }
    return ptr;
}

static inline void _aligned_free(void * ptr)
{
    free(ptr);
}
#endif
