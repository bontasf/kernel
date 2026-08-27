#ifndef _SYSTEM_HEAP_H
#define _SYSTEM_HEAP_H

#include "platform_types.h"

STATUS API SystemHeapInit(VOID);

STATUS API SystemHeapAllocate(OUT VOID **Buffer, IN CONST UINT64 Size, IN CHAR16 Tag[4]);

STATUS API SystemHeapFree(IN OUT VOID **Buffer);

#endif /* _SYSTEM_HEAP_H */
