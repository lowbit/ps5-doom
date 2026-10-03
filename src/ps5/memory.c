#include <stdint.h>

#include "platform.h"
#include "ps5.h"
#include "sce.h"

#define CACHE_LINE 64

void *ps5_gpu_memory(size_t bytes)
{
    int64_t physical = 0;
    void *address = NULL;
    int result;

    result = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), bytes,
                                           SCE_MEMORY_ALIGN, SCE_MEMORY_GPU_SHARED, &physical);
    if (result < 0)
    {
        plat_log("ps5: direct memory allocation of %zu bytes failed %#x\n", bytes, result);
        return NULL;
    }
    result = sceKernelMapDirectMemory(&address, bytes, SCE_PROT_CPU_GPU_RW, 0, physical,
                                      SCE_MEMORY_ALIGN);
    if (result < 0)
    {
        plat_log("ps5: direct memory mapping of %zu bytes failed %#x\n", bytes, result);
        return NULL;
    }
    return address;
}

void ps5_cache_flush(const volatile void *address, size_t bytes)
{
    uintptr_t at = (uintptr_t)address & ~(uintptr_t)(CACHE_LINE - 1);
    uintptr_t end = (uintptr_t)address + bytes;

    for (; at < end; at += CACHE_LINE)
        __builtin_ia32_clflush((const void *)at);
    __builtin_ia32_mfence();
}
