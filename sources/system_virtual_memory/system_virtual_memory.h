#ifndef _SYSTEM_VIRTUAL_MEMORY_H_
#define _SYSTEM_VIRTUAL_MEMORY_H_

#include "platform_types.h"
#include "system_memory_types.h"

#define SYSTEM_VIRTUAL_MEMORY_FLAG_READ_WRITE       (1ULL << 0)
#define SYSTEM_VIRTUAL_MEMORY_FLAG_EXEC             (1ULL << 2)
#define SYSTEM_VIRTUAL_MEMORY_FLAG_GLOBAL_ENABLED   (1ULL << 3)

typedef struct _VIRTUAL_MEMORY_SPACE VIRTUAL_MEMORY_SPACE;

typedef enum _MEMORY_CACHE_TYPE
{
    MemoryCacheUncacheable,
    MemoryCacheWriteCombining,
    MemoryCacheWriteThrough,
    MemoryCacheWriteBack,
    MemoryCacheWriteProtected,
    MemoryCacheUncacheableMinus
} MEMORY_CACHE_TYPE;

STATUS API SystemVirtualMemoryInit(IN SYSTEM_MEMORY *SystemMemory);

// STATUS API SystemVirtualMemoryCreateSpace(OUT VIRTUAL_MEMORY_SPACE **VirtualMemorySpace);

STATUS API SystemVirtualMemoryDestroySpace(IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace);

// STATUS API SystemVirtualMemorySwitchSpace(IN CONST VIRTUAL_MEMORY_SPACE *VirtualMemorySpace);

// STATUS API SystemVirtualMemoryGetCurrentSpace(OUT VIRTUAL_MEMORY_SPACE **VirtualMemorySpace);

STATUS API SystemVirtualMemoryMapPages(IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace, IN UINT64 VirtualAddress, IN UINT64 PhysicalAddress, IN UINT64 PageCount, IN UINT64 Flags, IN MEMORY_CACHE_TYPE MemoryCacheType);

STATUS API SystemVirtualMemoryUnmapPages(IN VIRTUAL_MEMORY_SPACE* VirtualMemorySpace, IN UINT64 VirtualAddress, IN UINT64 PageCount);

// STATUS API SystemVirtualMemoryUpdateFlags(IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace, IN UINT64 VirtualAddress, IN UINT64 PageCount, IN UINT64 Flags, MEMORY_CACHE_TYPE MemoryCacheType);

// STATUS API SystemVirtualMemoryQuery(IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace, IN UINT64 VirtualAddress, OUT UINT64 *PhysicalAddress, OUT UINT64* Flags, OUT MEMORY_CACHE_TYPE *MemoryCacheType);

// STATUS API SystemVirtualMemoryIsMapped(IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace, IN UINT64 VirtualAddress, OUT BOOLEAN *IsMapped);

#endif /* _SYSTEM_VIRTUAL_MEMORY_H_ */
