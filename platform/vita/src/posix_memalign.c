// PlayStation Vita: newlib declares posix_memalign in <stdlib.h> but does not implement it (checked against
// the 2026.08 SDK's libc.a, which has memalign and aligned_alloc only). Jolt's aligned allocator calls it
// (Jolt/Core/Memory.cpp), so this file supplies it on top of memalign; the result is freed with free(), as
// Jolt does, which newlib allows for memalign'd blocks. Compiled only for Vita (see cmake/Vita.cmake).

#include <errno.h>
#include <malloc.h>
#include <stdlib.h>

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    // POSIX: a power of two that is a multiple of sizeof(void*), else EINVAL.
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 || alignment % sizeof(void*) != 0) {
        return EINVAL;
    }
    void* block = memalign(alignment, size);
    if (!block) {
        return ENOMEM;
    }
    *memptr = block;
    return 0;
}
