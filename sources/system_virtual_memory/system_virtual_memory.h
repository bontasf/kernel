#ifndef _SYSTEM_VIRTUAL_MEMORY_H_
#define _SYSTEM_VIRTUAL_MEMORY_H_

#include "platform_types.h"
#include "system_memory_types.h"

STATUS API SystemVirtualMemoryInit(IN UINT64 KernelPhysicalBaseAddress, IN UINT64 KernelVirtualBaseAddress);

STATUS API SystemVirtualMemoryAllocatePages(OUT UINT64 *VirtualAddress, IN CONST UINT64 NumberOfPages);

STATUS API SystemVirtualMemoryFreePages(IN OUT UINT64 *VirtualAddress, IN CONST UINT64 NumberOfPages);

#endif /* _SYSTEM_VIRTUAL_MEMORY_H_ */