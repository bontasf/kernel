#ifndef _SYSTEM_PHYSICAL_MEMORY_H_
#define _SYSTEM_PHYSICAL_MEMORY_H_

#include "platform_types.h"
#include "system_memory_types.h"

STATUS API SystemPhysicalMemoryInit(IN SYSTEM_MEMORY *SystemMemory);

STATUS API SystemPhysicalMemoryAllocatePages(OUT UINT64 *PhysicalAddress, IN CONST UINT64 PageCount);

STATUS API SystemPhysicalMemoryFreePages(IN OUT UINT64 *PhysicalAddress);

STATUS API SystemPhysicalMemoryIncreaseReferenceCount(UINT64 PhysicalAddress);

STATUS API SystemPhysicalMemoryDecreaseReferenceCount(UINT64 PhysicalAddress);

STATUS API SystemPhysicalMemoryAllocatePool(OUT VOID **Buffer, IN CONST UINT64 Size);

STATUS API SystemPhysicalMemoryFreePool(IN OUT VOID **Buffer, IN CONST UINT64 Size);

#endif /* _SYSTEM_PHYSICAL_MEMORY_H_ */
