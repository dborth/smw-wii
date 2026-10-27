// Allocator for SDL on Wii: big, long-lived data (surface pixels, decoded audio, ...) goes to MEM2; the C library
// heap (malloc/new, DMA/GX buffers the SDL drivers memalign) stays in MEM1.
//
// Why: libogc2's malloc_wii.c grabs the whole MEM1 arena as ONE fixed-size mspace and, with the default
// MALLOC_MEM2 = 1, malloc()/new use only that. There is no spill-over into MEM2, so when MEM1 is full malloc()
// returns NULL while 64 MB of MEM2 is idle. mem2_*() are libogc2's allocators for the (growable) MEM2 mspace.
//
// The free/realloc functions decide by ADDRESS, never by assumption: SDL (the fork's audio driver, for one) frees
// memalign()'d MEM1 blocks with SDL_free(), and libogc2 is built without dlmalloc FOOTERS, so handing a MEM1 pointer
// to mem2_free() would corrupt the heap.
//
// Install with SDL_SetMemoryFunctions(smw_mem::malloc, smw_mem::calloc, smw_mem::realloc, smw_mem::free),
// before the first SDL call (a constructor in wii_paths.cpp does that).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" {   // libogc2, machine/malloc.h
void* mem2_malloc(size_t bytes);
void* mem2_calloc(size_t n_elements, size_t elem_size);
void* mem2_realloc(void* oldmem, size_t bytes);
void mem2_free(void* mem);
}

#ifndef SMW_MEM2_LO   // cached MEM2 window (the host test overrides these)
#define SMW_MEM2_LO 0x90000000u
#define SMW_MEM2_HI 0x94000000u
#endif

namespace smw_mem {

inline bool in_mem2(const void* p)
{
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    return a >= static_cast<uintptr_t>(SMW_MEM2_LO) && a < static_cast<uintptr_t>(SMW_MEM2_HI);
}

// MEM2 first; if MEM2 can't satisfy it, fall back to the MEM1 heap rather than fail.
inline void* malloc(size_t n)
{
    void* p = mem2_malloc(n);
    return p ? p : std::malloc(n);
}

inline void* calloc(size_t count, size_t size)
{
    void* p = mem2_calloc(count, size);
    return p ? p : std::calloc(count, size);
}

inline void* realloc(void* old, size_t n)
{
    if (!old) return smw_mem::malloc(n);
    if (in_mem2(old)) return mem2_realloc(old, n);   // stays in MEM2 (NULL if MEM2 is full; the old block is kept)
    return std::realloc(old, n);                     // MEM1 blocks stay in MEM1
}

inline void free(void* p)
{
    if (!p) return;
    if (in_mem2(p)) mem2_free(p);
    else std::free(p);
}

} // namespace smw_mem
