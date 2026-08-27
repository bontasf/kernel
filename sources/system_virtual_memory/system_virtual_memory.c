#include "system_virtual_memory.h"

#include "utils.h"
#include "system_physical_memory.h"
#include "msr.h"
#include "system_log.h"

#define MODULE_TAG u"SYSTEM_VIRTUAL_MEMORY"

#define HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS     (0xFFFF800000000000ULL)
#define HIGHER_HALF_KERNEL_IMAGE                        (0xFFFFFFFF80000000ULL)
#define HIGHER_HALF_KERNEL_STACK                        (0xFFFFFFFFFFFFF000ULL)
#define HIGHER_HALF_KERNEL_STACK_SIZE                   (16 * 1024)
#define HIGHER_HALF_KERNEL_STACK_BASE                   (HIGHER_HALF_KERNEL_STACK - HIGHER_HALF_KERNEL_STACK_SIZE)

#define PAGE_ATTRIBUTE_DECODING_TABLE_MSR 0x277

#define PRESENT_MASK            (1ULL << 0ULL)
#define READ_WRITE_MASK         (1ULL << 1ULL)
#define USER_SUPERVISOR_MASK    (1ULL << 2ULL)
#define GLOBAL_ENABLE_MASK      (1ULL << 8ULL)
#define EXECUTE_DISABLE_MASK    (1ULL << 63ULL)

#define SLOT_PER_SLAB (126ULL)

typedef struct _VIRTUAL_MEMORY_SPACE
{
    UINT64 PageMapLevel4PhysicalAddress;
    UINT64 PageMapLevel4VirtualAddress;
    UINT64 Flags;
    UINT32 ReferenceCount;
} VIRTUAL_MEMORY_SPACE;

typedef struct _VIRTUAL_MEMORY_SPACE_SLAB VIRTUAL_MEMORY_SPACE_SLAB;

typedef struct _VIRTUAL_MEMORY_SPACE_SLAB
{
    VIRTUAL_MEMORY_SPACE_SLAB *Next;
    VIRTUAL_MEMORY_SPACE_SLAB *Previous;
    UINT16                     FreeCount;
    UINT8                      Reserved0[6];
    UINT64                     FreeBitmap[2];
    UINT8                      Reserved1[24];
    VIRTUAL_MEMORY_SPACE       Slots[SLOT_PER_SLAB];
} VIRTUAL_MEMORY_SPACE_SLAB;

COMPILE_ASSERT(sizeof(VIRTUAL_MEMORY_SPACE_SLAB) == 4096, VIRTUAL_MEMORY_SLAB_SIZE_IS_4096);

typedef enum PAGE_LEVEL
{
    VirtualMemoryPageLevelPageMapLevel4,
    VirtualMemoryPageLevelPageDirectoryPointerTable,
    VirtualMemoryPageLevelPageDirectory,
    VirtualMemoryPageLevelPageTable,
    VirtualMemoryPageLevelInvalid
} PAGE_LEVEL;

typedef struct _WALK_RESULT
{
    UINT64 *Entry;
    PAGE_LEVEL PageLevel;
} WALK_RESULT;

VIRTUAL_MEMORY_SPACE KernelMemorySpace =
{
    .PageMapLevel4PhysicalAddress = 0,
    .PageMapLevel4VirtualAddress = 0,
    .ReferenceCount = 0,
    .Flags = 0
};

extern STATUS API HigherHafKernelEntry(VOID);

static STATUS API WalkVirtualMemorySpace
(
    IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace,
    IN UINT64 VirtualAddress,
    IN BOOLEAN CreateIfMissing,
    OUT WALK_RESULT *WalkResult
);

static VOID API SetPageAttributeTable(VOID);

static VOID API JumpToHigherHalf(UINT64 PageMapLevel4PhysicalAddress, UINT64 KernelStackTop);

static STATUS API InitVirtualMemorySpace(IN OUT VIRTUAL_MEMORY_SPACE *VirtualMemorySpace);

static VOID API InitVirtualMemorySpaceSlab(IN OUT VIRTUAL_MEMORY_SPACE_SLAB* Slab);

static BOOLEAN API IsUserspaceAddress(CONST UINT64 VirtualAddress);

extern STATUS API HigherHafKernelEntry(VOID);

UINT64 KernelBaseAddress = 0;

extern UINT64 KernelDirectMappingVirtualOffset;
extern STATUS API SystemPhysicalMemoryInternalVirtualSwitch(UINT64 VirtualBase);

static VIRTUAL_MEMORY_SPACE_SLAB *VirtualMemorySpaceSlab;

STATUS API SystemVirtualMemoryInit(IN SYSTEM_MEMORY *SystemMemory)
{
    STATUS Status = E_OK;
    UINT64 KernelStackBasePhysicalAddress = 0;
    UINT64 NumberOfPages = HIGHER_HALF_KERNEL_STACK_SIZE / PHYSICAL_MEMORY_PAGE_SIZE;

    if (NULL_PTR == SystemMemory)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    SetPageAttributeTable();

    KernelMemorySpace.PageMapLevel4PhysicalAddress = 0;
    KernelMemorySpace.PageMapLevel4VirtualAddress = 0;
    KernelMemorySpace.ReferenceCount = 1;
    KernelMemorySpace.Flags = 0;
    VirtualMemorySpaceSlab = NULL_PTR;

    Status = SystemPhysicalMemoryAllocatePages(&KernelMemorySpace.PageMapLevel4PhysicalAddress, 1);
    if (E_OK != Status)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
    MemorySet((VOID *)PhysicalToVirtual(KernelMemorySpace.PageMapLevel4PhysicalAddress), 0U, PHYSICAL_MEMORY_PAGE_SIZE);

    Status = SystemPhysicalMemoryAllocatePages(&KernelStackBasePhysicalAddress, NumberOfPages);
    if (E_OK != Status)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
    MemorySet((VOID *)PhysicalToVirtual(KernelStackBasePhysicalAddress), 0U, PHYSICAL_MEMORY_PAGE_SIZE * NumberOfPages);

    KernelMemorySpace.PageMapLevel4VirtualAddress = PhysicalToVirtual(KernelMemorySpace.PageMapLevel4PhysicalAddress);
    Status = SystemVirtualMemoryMapPages
    (
        &KernelMemorySpace,
        HIGHER_HALF_KERNEL_STACK_BASE,
        KernelStackBasePhysicalAddress,
        NumberOfPages,
        SYSTEM_VIRTUAL_MEMORY_FLAG_READ_WRITE | SYSTEM_VIRTUAL_MEMORY_FLAG_GLOBAL_ENABLED,
        MemoryCacheWriteBack
    );
    if (E_OK != Status)
    {
        goto Cleanup;
    }

    /* Higher Half Direct Map */
    for (UINT64 DescriptorIndex = 0; DescriptorIndex < SystemMemory->NumberOfMemoryDescriptors; DescriptorIndex ++)
    {
        /* Still using bootloader memory, will be removed in the future */
        if ((MemoryUsable == SystemMemory->MemoryDescriptors[DescriptorIndex].Type) ||
            (MemoryBootloader == SystemMemory->MemoryDescriptors[DescriptorIndex].Type) ||
            (MemoryACPI == SystemMemory->MemoryDescriptors[DescriptorIndex].Type))
        {
            MEMORY_REGION_DESCRIPTOR *Descriptor = &SystemMemory->MemoryDescriptors[DescriptorIndex];
            Status = SystemVirtualMemoryMapPages
            (
                &KernelMemorySpace,
                HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS + Descriptor->PhysicalStart,
                Descriptor->PhysicalStart,
                Descriptor->PageCount,
                SYSTEM_VIRTUAL_MEMORY_FLAG_READ_WRITE | SYSTEM_VIRTUAL_MEMORY_FLAG_GLOBAL_ENABLED,
                MemoryCacheWriteBack
            );

            if (E_OK != Status)
            {
                goto Cleanup;
            }
        }

        else if (MemoryFramebuffer == SystemMemory->MemoryDescriptors[DescriptorIndex].Type)
        {
            MEMORY_REGION_DESCRIPTOR *Descriptor = &SystemMemory->MemoryDescriptors[DescriptorIndex];
            Status = SystemVirtualMemoryMapPages
            (
                &KernelMemorySpace,
                HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS + Descriptor->PhysicalStart,
                Descriptor->PhysicalStart,
                Descriptor->PageCount,
                SYSTEM_VIRTUAL_MEMORY_FLAG_READ_WRITE | SYSTEM_VIRTUAL_MEMORY_FLAG_GLOBAL_ENABLED,
                MemoryCacheWriteCombining
            );

            if (E_OK != Status)
            {
                goto Cleanup;
            }
        }

        else if (MemoryKernelImage == SystemMemory->MemoryDescriptors[DescriptorIndex].Type)
        {
            MEMORY_REGION_DESCRIPTOR *Descriptor = &SystemMemory->MemoryDescriptors[DescriptorIndex];
            Status = SystemVirtualMemoryMapPages
            (
                &KernelMemorySpace,
                HIGHER_HALF_KERNEL_IMAGE,
                Descriptor->PhysicalStart,
                Descriptor->PageCount,
                SYSTEM_VIRTUAL_MEMORY_FLAG_READ_WRITE | SYSTEM_VIRTUAL_MEMORY_FLAG_GLOBAL_ENABLED | SYSTEM_VIRTUAL_MEMORY_FLAG_EXEC,
                MemoryCacheWriteBack
            );

            if (E_OK != Status)
            {
                goto Cleanup;
            }

            /* Temporary memory map */
            Status = SystemVirtualMemoryMapPages
            (
                &KernelMemorySpace,
                Descriptor->PhysicalStart,
                Descriptor->PhysicalStart,
                Descriptor->PageCount,
                SYSTEM_VIRTUAL_MEMORY_FLAG_READ_WRITE | SYSTEM_VIRTUAL_MEMORY_FLAG_GLOBAL_ENABLED | SYSTEM_VIRTUAL_MEMORY_FLAG_EXEC,
                MemoryCacheWriteBack
            );

            if (E_OK != Status)
            {
                goto Cleanup;
            }

            KernelBaseAddress = Descriptor->PhysicalStart;
        }
    }

    KernelDirectMappingVirtualOffset = HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS;
    KernelMemorySpace.PageMapLevel4VirtualAddress = HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS + KernelMemorySpace.PageMapLevel4PhysicalAddress;
    LOG_INFO(u"Kernel Base Address: %x", KernelBaseAddress);
    SystemPhysicalMemoryInternalVirtualSwitch(HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS);
    JumpToHigherHalf(KernelMemorySpace.PageMapLevel4PhysicalAddress, HIGHER_HALF_KERNEL_STACK);
    Cleanup:
    if (E_NOT_OK == Status)
    {
        if (0 != KernelMemorySpace.PageMapLevel4PhysicalAddress)
        {
            SystemVirtualMemoryDestroySpace(&KernelMemorySpace);
        }
    }
    return Status;
}

STATUS API SystemVirtualMemoryCreateSpace(OUT VIRTUAL_MEMORY_SPACE **VirtualMemorySpace)
{
    STATUS Status = E_OK;
    UINT64 PhysicalAddress = 0;
    VIRTUAL_MEMORY_SPACE_SLAB *Slab = NULL_PTR;
    VIRTUAL_MEMORY_SPACE_SLAB *AllocateSlab = NULL_PTR;
    UINT16 SlotIndex = 0;
    UINT8 BitmapSelector = 0;
    UINT8 BitmapBit = 0;

    if (NULL_PTR == VirtualMemorySpace)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (NULL_PTR != *VirtualMemorySpace)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (NULL_PTR == VirtualMemorySpaceSlab)
    {
        Status = SystemPhysicalMemoryAllocatePages(&PhysicalAddress, 1);
        if (E_OK != Status)
        {
            goto Cleanup;
        }

        AllocateSlab = (VIRTUAL_MEMORY_SPACE_SLAB *)(HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS + PhysicalAddress);
        InitVirtualMemorySpaceSlab(AllocateSlab);
        VirtualMemorySpaceSlab = AllocateSlab;
    }

    Slab = VirtualMemorySpaceSlab;
    while ((0 == Slab->FreeCount) && (NULL_PTR != Slab->Next))
    {
        Slab = Slab->Next;
    }

    if (0 == Slab->FreeCount)
    {
        Status = SystemPhysicalMemoryAllocatePages(&PhysicalAddress, 1);
        if (E_OK != Status)
        {
            goto Cleanup;
        }

        AllocateSlab = (VIRTUAL_MEMORY_SPACE_SLAB *)(HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS + PhysicalAddress);
        InitVirtualMemorySpaceSlab(AllocateSlab);
        AllocateSlab->Previous = Slab;
        Slab->Next = AllocateSlab;
        Slab = AllocateSlab;
    }

    if (0 == Slab->FreeCount)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 != Slab->FreeBitmap[0])
    {
        BitmapSelector = 0;
        BitmapBit = CountTrailingZeros(Slab->FreeBitmap[0]);
    }
    else
    {
        BitmapSelector = 1;
        BitmapBit = CountTrailingZeros(Slab->FreeBitmap[1]);
    }

    Slab->FreeBitmap[BitmapSelector] &= (~(1ULL << BitmapBit));
    Slab->FreeCount--;
    SlotIndex = BitmapSelector * 64 + BitmapBit;
    Status = InitVirtualMemorySpace(&Slab->Slots[SlotIndex]);
    if (E_OK != Status)
    {
        Slab->FreeBitmap[BitmapSelector] |= (1ULL << BitmapBit);
        Slab->FreeCount++;
        goto Cleanup;
    }

    *VirtualMemorySpace = &Slab->Slots[SlotIndex];

Cleanup:
    return Status;
}

STATUS API SystemVirtualMemoryDestroySpace(IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace)
{
    return E_NOT_OK;
}

STATUS API SystemVirtualMemorySwitchSpace(IN CONST VIRTUAL_MEMORY_SPACE *VirtualMemorySpace)
{
    STATUS Status = E_OK;

    if (NULL_PTR == VirtualMemorySpace)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 == VirtualMemorySpace->PageMapLevel4PhysicalAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    ASM("mov %0, %%cr3" :: "r"(VirtualMemorySpace->PageMapLevel4PhysicalAddress) : "memory");

Cleanup:
    return Status;
}

STATUS API SystemVirtualMemoryGetCurrentSpace(OUT VIRTUAL_MEMORY_SPACE **VirtualMemorySpace)
{
    return E_NOT_OK;
}

STATUS API SystemVirtualMemoryMapPages(IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace, IN UINT64 VirtualAddress, IN UINT64 PhysicalAddress, IN UINT64 PageCount, IN UINT64 Flags, IN MEMORY_CACHE_TYPE MemoryCacheType)
{
    STATUS Status = E_OK;
    WALK_RESULT WalkResult =
    {
        .Entry = NULL_PTR,
        .PageLevel = VirtualMemoryPageLevelInvalid
    };
    UINT64 CurrentPhysicalAddress = 0;
    UINT64 CurrentVirtualAddress = 0;
    UINT64 FailedMapIndex = 0;
    UINT64 PageTableEntry = 0;

    if (NULL_PTR == VirtualMemorySpace)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 != (VirtualAddress % PHYSICAL_MEMORY_PAGE_SIZE))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 != (PhysicalAddress % PHYSICAL_MEMORY_PAGE_SIZE))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 == PageCount)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    CurrentPhysicalAddress = PhysicalAddress;
    CurrentVirtualAddress = VirtualAddress;
    for (UINT64 PageIndex = 0; PageIndex < PageCount ; PageIndex ++)
    {
        Status = WalkVirtualMemorySpace(VirtualMemorySpace, CurrentVirtualAddress, TRUE, &WalkResult);
        if (E_NOT_OK == Status)
        {
            FailedMapIndex = PageIndex;
            goto Cleanup;
        }

        if (VirtualMemoryPageLevelPageTable != WalkResult.PageLevel)
        {
            /* Currently not supported */
            FailedMapIndex = PageIndex;
            Status = E_NOT_OK;
            goto Cleanup;
        }

        if (0 != ((*(WalkResult.Entry) & 1)))
        {
            FailedMapIndex = PageIndex;
            goto Cleanup;
        }

        PageTableEntry = (CurrentPhysicalAddress & 0x000FFFFFFFFFF000ULL);
        PageTableEntry |= PRESENT_MASK;
        PageTableEntry |= (((UINT64)(MemoryCacheType) >> 0ULL) & 1ULL) << 3ULL;
        PageTableEntry |= (((UINT64)(MemoryCacheType) >> 1ULL) & 1ULL) << 4ULL;
        PageTableEntry |= (((UINT64)(MemoryCacheType) >> 2ULL) & 1ULL) << 7ULL;
        if (0 != (Flags & SYSTEM_VIRTUAL_MEMORY_FLAG_READ_WRITE))
        {
            PageTableEntry |= READ_WRITE_MASK;
        }
        if (0 == (Flags & SYSTEM_VIRTUAL_MEMORY_FLAG_EXEC))
        {
            PageTableEntry |= EXECUTE_DISABLE_MASK;
        }
        if (0 != (Flags & SYSTEM_VIRTUAL_MEMORY_FLAG_GLOBAL_ENABLED))
        {
            PageTableEntry |= GLOBAL_ENABLE_MASK;
        }
        if (TRUE == IsUserspaceAddress(CurrentVirtualAddress))
        {
            PageTableEntry |= USER_SUPERVISOR_MASK;
        }

        *(WalkResult.Entry) = PageTableEntry;
        CurrentPhysicalAddress += PHYSICAL_MEMORY_PAGE_SIZE;
        CurrentVirtualAddress += PHYSICAL_MEMORY_PAGE_SIZE;
    }

Cleanup:
    if (E_OK != Status)
    {
        SystemVirtualMemoryUnmapPages(VirtualMemorySpace, VirtualAddress, FailedMapIndex);
    }

    return Status;
}

STATUS API SystemVirtualMemoryUnmapPages(IN VIRTUAL_MEMORY_SPACE* VirtualMemorySpace, IN UINT64 VirtualAddress, IN UINT64 PageCount)
{
    return E_NOT_OK;
}

STATUS API SystemVirtualMemoryUpdateFlags(IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace, IN UINT64 VirtualAddress, IN UINT64 PageCount, IN UINT64 Flags, MEMORY_CACHE_TYPE MemoryCacheType)
{
    return E_NOT_OK;
}


STATUS API SystemVirtualMemoryQuery
(
    IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace,
    IN UINT64 VirtualAddress,
    OUT OPTIONAL UINT64 *PhysicalAddress,
    OUT OPTIONAL OPTIONAL UINT64* Flags,
    OUT OPTIONAL MEMORY_CACHE_TYPE *MemoryCacheType
)
{
    STATUS Status = E_OK;
    WALK_RESULT WalkResult =
    {
        .Entry = NULL_PTR,
        .PageLevel = VirtualMemoryPageLevelInvalid
    };

    if (NULL_PTR == VirtualMemorySpace)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 == VirtualAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Status = WalkVirtualMemorySpace(VirtualMemorySpace, VirtualAddress, FALSE, &WalkResult);
    if (E_OK != Status)
    {
        goto Cleanup;
    }

    if (VirtualMemoryPageLevelPageTable != WalkResult.PageLevel)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 == ((*WalkResult.Entry) & PRESENT_MASK))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (NULL_PTR != PhysicalAddress)
    {
        *PhysicalAddress = (*WalkResult.Entry) & 0x000FFFFFFFFFF000ULL;
    }

    if (NULL_PTR != Flags)
    {
        *Flags = 0;
        if (0 != ((*WalkResult.Entry) & READ_WRITE_MASK))
        {
            (*Flags) |= SYSTEM_VIRTUAL_MEMORY_FLAG_READ_WRITE;
        }
        if (0 != ((*WalkResult.Entry) & GLOBAL_ENABLE_MASK))
        {
            (*Flags) |= SYSTEM_VIRTUAL_MEMORY_FLAG_GLOBAL_ENABLED;
        }
        if (0 == ((*WalkResult.Entry) & EXECUTE_DISABLE_MASK))
        {
            (*Flags) |= SYSTEM_VIRTUAL_MEMORY_FLAG_EXEC;
        }
    }

    if (NULL_PTR != MemoryCacheType)
    {
        *(MemoryCacheType) = (MEMORY_CACHE_TYPE)
        (
            ((((*WalkResult.Entry) >> 3) & 1ULL) << 0ULL) |
            ((((*WalkResult.Entry) >> 4) & 1ULL) << 1ULL) |
            ((((*WalkResult.Entry) >> 7) & 1ULL) << 2ULL)
        );
    }

Cleanup:
    if (E_OK != Status)
    {
        if (NULL_PTR != PhysicalAddress)
        {
            *PhysicalAddress = 0ULL;
        }
        if (NULL_PTR != Flags)
        {
            *Flags = 0ULL;
        }
    }
    return Status;
}

STATUS API SystemVirtualMemoryIsMapped(IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace, IN UINT64 VirtualAddress, OUT BOOLEAN *IsMapped)
{
    STATUS Status = E_OK;
    WALK_RESULT WalkResult =
    {
        .Entry = NULL_PTR,
        .PageLevel = VirtualMemoryPageLevelInvalid
    };

    if (NULL_PTR == VirtualMemorySpace)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (NULL_PTR == IsMapped)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 == VirtualAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Status = WalkVirtualMemorySpace(VirtualMemorySpace, VirtualAddress, FALSE, &WalkResult);
    if ((E_OK != Status) || (0 == ((*WalkResult.Entry) & PRESENT_MASK)))
    {
        *IsMapped = FALSE;
    }
    else
    {
        *IsMapped = TRUE;
    }

    Status = E_OK;

Cleanup:
    return Status;
}

static STATUS API InitVirtualMemorySpace(IN OUT VIRTUAL_MEMORY_SPACE *VirtualMemorySpace)
{
    STATUS Status = E_OK;
    UINT64 *PageMapLevel4 = NULL_PTR;
    UINT64 *KernelPageMapLevel4 = NULL_PTR;

    if (NULL_PTR == VirtualMemorySpace)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 == KernelMemorySpace.PageMapLevel4VirtualAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    VirtualMemorySpace->PageMapLevel4PhysicalAddress = 0;
    VirtualMemorySpace->PageMapLevel4VirtualAddress = 0;
    VirtualMemorySpace->Flags = 0;
    VirtualMemorySpace->ReferenceCount = 0;

    Status = SystemPhysicalMemoryAllocatePages(&VirtualMemorySpace->PageMapLevel4PhysicalAddress, 1);
    if (E_OK != Status)
    {
        goto Cleanup;
    }

    VirtualMemorySpace->PageMapLevel4VirtualAddress = HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS + VirtualMemorySpace->PageMapLevel4PhysicalAddress;
    PageMapLevel4 = (UINT64 *)VirtualMemorySpace->PageMapLevel4VirtualAddress;
    KernelPageMapLevel4 = (UINT64 *)KernelMemorySpace.PageMapLevel4VirtualAddress;

    for (UINT64 PageMapLevel4Index = 0; PageMapLevel4Index < 256; PageMapLevel4Index ++)
    {
        PageMapLevel4[PageMapLevel4Index] = 0;
    }

    /* Copy Higher Half entries into the Virtual Region */
    for (UINT64 PageMapLevel4Index = 256; PageMapLevel4Index < 512; PageMapLevel4Index ++)
    {
        PageMapLevel4[PageMapLevel4Index] = KernelPageMapLevel4[PageMapLevel4Index];
    }

Cleanup:
    return Status;
}

static VOID API InitVirtualMemorySpaceSlab(IN OUT VIRTUAL_MEMORY_SPACE_SLAB* Slab)
{
    Slab->Next = NULL_PTR;
    Slab->Previous = NULL_PTR;
    Slab->FreeBitmap[0] = 0xFFFFFFFFFFFFFFFFULL;
    Slab->FreeBitmap[1] = 0x3FFFFFFFFFFFFFFFULL;
    Slab->FreeCount = SLOT_PER_SLAB;
}

static BOOLEAN API IsUserspaceAddress(CONST UINT64 VirtualAddress)
{
    return VirtualAddress < HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS;
}

static STATUS API WalkVirtualMemorySpace
(
    IN VIRTUAL_MEMORY_SPACE *VirtualMemorySpace,
    IN UINT64 VirtualAddress,
    IN BOOLEAN CreateIfMissing,
    OUT WALK_RESULT *WalkResult
)
{
    STATUS Status = E_OK;
    UINT64 PageMapLevel4PhysicalAddress = 0;
    UINT64 PageDirectoryPointerTablePhysicalAddress = 0;
    UINT64 PageDirectoryPhysicalAddress = 0;
    UINT64 PageTablePhysicalAddress = 0;

    UINT64 PageDirectoryPointerTableResult = 0;
    UINT64 PageDirectoryResult = 0;
    UINT64 PageTableResult = 0;

    BOOLEAN PageDirectoryPointerTableMarkForFreeOnFailure = FALSE;
    BOOLEAN PageDirectoryMarkForFreeOnFailure = FALSE;
    BOOLEAN PageTableMarkForFreeOnFailure = FALSE;

    UINT16 PageMapLevel4Index = (VirtualAddress >> 39) & 0x1FF;
    UINT16 PageDirectoryPointerTableIndex = (VirtualAddress >> 30) & 0x1FF;
    UINT16 PageDirectoryIndex = (VirtualAddress >> 21) & 0x1FF;
    UINT16 PageTableIndex = (VirtualAddress >> 12) & 0x1FF;

    UINT64 *PageMapLevel4 = NULL_PTR;
    UINT64 *PageDirectoryPointerTable = NULL_PTR;
    UINT64 *PageDirectory = NULL_PTR;
    UINT64 *PageTable = NULL_PTR;

    if (0ULL == VirtualMemorySpace->PageMapLevel4PhysicalAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    PageMapLevel4 = (UINT64 *) VirtualMemorySpace->PageMapLevel4VirtualAddress;
    if (0ULL == (PageMapLevel4[PageMapLevel4Index] & 0x1ULL))
    {
        if (TRUE == CreateIfMissing)
        {
            Status = SystemPhysicalMemoryAllocatePages(&PageDirectoryPointerTablePhysicalAddress, 1);
            if (E_OK != Status)
            {
                goto Cleanup;
            }
            PageDirectoryPointerTableMarkForFreeOnFailure = TRUE;
            MemorySet((VOID *)PhysicalToVirtual(PageDirectoryPointerTablePhysicalAddress), 0, PHYSICAL_MEMORY_PAGE_SIZE);
            PageDirectoryPointerTableResult = PageDirectoryPointerTablePhysicalAddress | READ_WRITE_MASK | PRESENT_MASK;
            if (TRUE == IsUserspaceAddress(VirtualAddress))
            {
                PageDirectoryPointerTableResult |= USER_SUPERVISOR_MASK;
            }
            PageMapLevel4[PageMapLevel4Index] = PageDirectoryPointerTableResult;
        }
        else
        {
            Status = E_NOT_OK;
            goto Cleanup;
        }
    }

    PageDirectoryPointerTable = (UINT64 *)PhysicalToVirtual(PageMapLevel4[PageMapLevel4Index] & (~0xFFFULL));
    if (0ULL == (PageDirectoryPointerTable[PageDirectoryPointerTableIndex] & 0x1ULL))
    {
        if (TRUE == CreateIfMissing)
        {
            Status = SystemPhysicalMemoryAllocatePages(&PageDirectoryPhysicalAddress, 1);
            if (E_OK != Status)
            {
                goto Cleanup;
            }
            PageDirectoryMarkForFreeOnFailure = TRUE;
            MemorySet((VOID *)PhysicalToVirtual(PageDirectoryPhysicalAddress), 0, PHYSICAL_MEMORY_PAGE_SIZE);
            PageDirectoryResult = PageDirectoryPhysicalAddress | READ_WRITE_MASK | PRESENT_MASK;
            if (TRUE == IsUserspaceAddress(VirtualAddress))
            {
                PageDirectoryResult |= USER_SUPERVISOR_MASK;
            }
            PageDirectoryPointerTable[PageDirectoryPointerTableIndex] = PageDirectoryResult;
        }
        else
        {
            Status = E_NOT_OK;
            goto Cleanup;
        }
    }

    PageDirectory = (UINT64 *)PhysicalToVirtual(PageDirectoryPointerTable[PageDirectoryPointerTableIndex] & (~0xFFFULL));
    if (0ULL == (PageDirectory[PageDirectoryIndex] & 0x1ULL))
    {
        if (TRUE == CreateIfMissing)
        {
            Status = SystemPhysicalMemoryAllocatePages(&PageTablePhysicalAddress, 1);
            if (E_OK != Status)
            {
                goto Cleanup;
            }
            PageTableMarkForFreeOnFailure = TRUE;
            MemorySet((VOID *)PhysicalToVirtual(PageTablePhysicalAddress), 0, PHYSICAL_MEMORY_PAGE_SIZE);
            PageTableResult = PageTablePhysicalAddress | READ_WRITE_MASK | PRESENT_MASK;
            if (TRUE == IsUserspaceAddress(VirtualAddress))
            {
                PageTableResult |= USER_SUPERVISOR_MASK;
            }
            PageDirectory[PageDirectoryIndex] = PageTableResult;
        }
        else
        {
            Status = E_NOT_OK;
            goto Cleanup;
        }
    }

    PageTable = (UINT64 *)PhysicalToVirtual(PageDirectory[PageDirectoryIndex] & (~0xFFFULL));

    WalkResult->Entry = &PageTable[PageTableIndex];
    WalkResult->PageLevel = VirtualMemoryPageLevelPageTable;
Cleanup:
    if (E_OK != Status)
    {
        if ((TRUE == PageTableMarkForFreeOnFailure) && (0 != PageTablePhysicalAddress))
        {
            if (NULL_PTR != PageDirectory)
            {
                PageDirectory[PageDirectoryIndex] = 0ULL;
            }
            SystemPhysicalMemoryFreePages(&PageTablePhysicalAddress);
            PageTable = NULL_PTR;
        }

        if ((TRUE == PageDirectoryMarkForFreeOnFailure) && (0 != PageDirectoryPhysicalAddress))
        {
            if (NULL_PTR != PageDirectoryPointerTable)
            {
                PageDirectoryPointerTable[PageDirectoryPointerTableIndex] = 0ULL;
            }
            SystemPhysicalMemoryFreePages(&PageDirectoryPhysicalAddress);
            PageDirectory = NULL_PTR;
        }

        if ((TRUE == PageDirectoryPointerTableMarkForFreeOnFailure) && (0 != PageDirectoryPointerTablePhysicalAddress))
        {
            if (NULL_PTR != PageMapLevel4)
            {
                PageMapLevel4[PageMapLevel4Index] = 0ULL;
            }
            SystemPhysicalMemoryFreePages(&PageDirectoryPointerTablePhysicalAddress);
            PageDirectoryPointerTable = NULL_PTR;
        }

        WalkResult->Entry = NULL_PTR;
        WalkResult->PageLevel = VirtualMemoryPageLevelInvalid;
    }

    return Status;
}

static VOID API SetPageAttributeTable(VOID)
{
    MsrWrite(PAGE_ATTRIBUTE_DECODING_TABLE_MSR, 0x0006070506040100ULL);
}

VOID NORETURN JumpToHigherHalf(UINT64 PageMapLevel4PhysicalAddress, UINT64 KernelStackTop)
{
    UINT64 CalculatedNewEntry = 0;
    CalculatedNewEntry =  HIGHER_HALF_KERNEL_IMAGE + ((UINT64)HigherHafKernelEntry - KernelBaseAddress);
    ASM(
        "cli\n\t"

        /* move inputs into known registers FIRST */
        "mov %0, %%rcx\n\t"
        "mov %1, %%rax\n\t"
        "mov %2, %%rbx\n\t"

        /* now do the switch */
        "mov %%rcx, %%cr3\n\t"
        "mov %%rax, %%rsp\n\t"

        /* ABI alignment fix for jmp */
        "sub $8, %%rsp\n\t"

        /* jump absolute */
        "jmp *%%rbx\n\t"
        :
        : "r"(PageMapLevel4PhysicalAddress),
          "r"(KernelStackTop),
          "r"(CalculatedNewEntry)
        : "rax", "rbx", "rcx", "memory"
    );
    __builtin_unreachable();
}
