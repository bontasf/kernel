#include "system_heap.h"

#include "system_physical_memory.h"
#include "system_virtual_memory.h"
#include "system_log.h"

#include "utils.h"

#define MODULE_TAG u"SYSTEM_HEAP"

#define HIGHER_HALF_KERNEL_HEAP         (0xFFFFA00000000000ULL)
#define HIGHER_HALF_KERNEL_HEAP_LIMIT   (0xFFFFC00000000000ULL)

#define FREE_BLOCK_MASK (1ULL << 0)

#define HEAP_MAGIC_ALLOCATED    (0x626F6E7461206F73ULL)
#define HEAP_MAGIC_FREE         (0x736F2061746E6F62ULL)

#define HEAP_MINIMUM_ALIGNMENT (16ULL)

#define HEAP_PHYSICAL_ALLOCATION_PAGE_COUNT (4ULL)

typedef struct _HEAP_BLOCK HEAP_BLOCK;

typedef struct _HEAP_BLOCK
{
    UINT64 Magic;
    UINT64 Size;
    UINT64 Flags;
    HEAP_BLOCK *Next;
    HEAP_BLOCK *Previous;
    UINT64 AllocaterTag;
} HEAP_BLOCK;

extern VIRTUAL_MEMORY_SPACE KernelMemorySpace;

static UINT64 PageCapacity = 0;
static HEAP_BLOCK *SystemHeap = NULL_PTR;

static inline UINT64 TagFromChar16(const CHAR16 Tag[4]);

static STATUS API IncreaseAllocatedHeap(HEAP_BLOCK *LastHeapBlock, UINT64 Size);

static STATUS API DecreraseAllocatedHeap(HEAP_BLOCK *LastHeapBlock);

STATUS API SystemHeapInit(VOID)
{
    STATUS Status = E_OK;
    UINT64 HeapPhysicalAddress = 0;

    PageCapacity = HEAP_PHYSICAL_ALLOCATION_PAGE_COUNT;
    Status = SystemPhysicalMemoryAllocatePages(&HeapPhysicalAddress, PageCapacity);
    if (E_OK != Status)
    {
        LOG_ERROR(u"Heap initial physical allocation failed");
        goto Cleanup;
    }

    Status = SystemVirtualMemoryMapPages
    (
        &KernelMemorySpace,
        HIGHER_HALF_KERNEL_HEAP,
        HeapPhysicalAddress,
        PageCapacity,
        SYSTEM_VIRTUAL_MEMORY_FLAG_READ_WRITE | SYSTEM_VIRTUAL_MEMORY_FLAG_GLOBAL_ENABLED,
        MemoryCacheWriteBack
    );
    if (E_OK != Status)
    {
        LOG_ERROR(u"Heap initial virtual mapping failed");
        goto Cleanup;
    }

    SystemHeap = (HEAP_BLOCK *)HIGHER_HALF_KERNEL_HEAP;
    SystemHeap->Magic = HEAP_MAGIC_FREE;
    SystemHeap->Size = PageCapacity * PHYSICAL_MEMORY_PAGE_SIZE - sizeof(*SystemHeap);
    SystemHeap->Flags = FREE_BLOCK_MASK;
    SystemHeap->Next = NULL_PTR;
    SystemHeap->Previous = NULL_PTR;
    SystemHeap->AllocaterTag = TagFromChar16(u"INIT");

    LOG_INFO(u"System Heap Initialized");
Cleanup:
    if (E_OK != Status)
    {
        if (0 != HeapPhysicalAddress)
        {
            SystemPhysicalMemoryFreePages(&HeapPhysicalAddress);
        }
    }

    return Status;
}

STATUS API SystemHeapAllocate(OUT VOID **Buffer, IN CONST UINT64 Size, IN CHAR16 Tag[4])
{
    STATUS Status = E_OK;
    UINT64 AllocatedSize = 0;
    BOOLEAN FoundFreeBlock = FALSE;
    HEAP_BLOCK *HeapBlock = NULL_PTR;
    HEAP_BLOCK *AllocatedHeapBlock = NULL_PTR;
    UINT64 Result = 0;

    if (NULL_PTR == SystemHeap)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (NULL_PTR == Buffer)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (NULL_PTR != *Buffer)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 == Size)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    AllocatedSize = ALIGN_UP(Size, HEAP_MINIMUM_ALIGNMENT);
    HeapBlock = SystemHeap;
    FoundFreeBlock = FALSE;

    while (FALSE == FoundFreeBlock)
    {
        if ((HEAP_MAGIC_FREE != HeapBlock->Magic) && (HEAP_MAGIC_ALLOCATED != HeapBlock->Magic))
        {
            /* Heap corrupted decline all requests, latter add kernel panic */
            Status = E_NOT_OK;
            SystemHeap = NULL_PTR;
            goto Cleanup;
        }

        if ((NULL_PTR == HeapBlock->Next) && ((HEAP_MAGIC_ALLOCATED == HeapBlock->Magic) || (HeapBlock->Size < AllocatedSize)))
        {
            Status = IncreaseAllocatedHeap(HeapBlock, AllocatedSize);
            if (Status != E_OK)
            {
                goto Cleanup;
            }
        }

        if ((HEAP_MAGIC_FREE == HeapBlock->Magic) && (HeapBlock->Size >= AllocatedSize))
        {
            if (HeapBlock->Size <= AllocatedSize + sizeof(*HeapBlock))
            {
                HeapBlock->Magic = HEAP_MAGIC_ALLOCATED;
                HeapBlock->Flags &= (~FREE_BLOCK_MASK);
                HeapBlock->AllocaterTag = TagFromChar16(Tag);
            }
            else
            {
                AllocatedHeapBlock = (HEAP_BLOCK *)((UINT64)HeapBlock + sizeof(*HeapBlock) + AllocatedSize);
                AllocatedHeapBlock->Magic = HEAP_MAGIC_FREE;
                AllocatedHeapBlock->Flags = FREE_BLOCK_MASK;
                AllocatedHeapBlock->Size = HeapBlock->Size - sizeof(*HeapBlock) - AllocatedSize;
                AllocatedHeapBlock->Next = HeapBlock->Next;
                if (NULL_PTR != HeapBlock->Next)
                {
                    HeapBlock->Next->Previous = AllocatedHeapBlock;
                }

                AllocatedHeapBlock->Previous = HeapBlock;
                AllocatedHeapBlock->AllocaterTag = TagFromChar16(u"HEAP");

                HeapBlock->Magic = HEAP_MAGIC_ALLOCATED;
                HeapBlock->Flags &= (~FREE_BLOCK_MASK);
                HeapBlock->Size = AllocatedSize;
                HeapBlock->Next = AllocatedHeapBlock;
                HeapBlock->AllocaterTag = TagFromChar16(Tag);

            }

            Result = (UINT64)HeapBlock + sizeof(*HeapBlock);
            FoundFreeBlock = TRUE;
        }
        else
        {
            HeapBlock = HeapBlock->Next;
        }
    }

    *Buffer = (VOID *)Result;
Cleanup:
    if (E_OK != Status)
    {
        if (NULL_PTR != Buffer)
        {
            *Buffer = NULL_PTR;
        }
    }

    return Status;
}

STATUS API SystemHeapFree(IN OUT VOID **Buffer)
{
    STATUS Status = E_OK;
    HEAP_BLOCK *HeapBlock = NULL_PTR;

    if (NULL_PTR == Buffer)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (NULL_PTR == *Buffer)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (((UINT64)(*Buffer) < (HIGHER_HALF_KERNEL_HEAP + sizeof(*HeapBlock))) ||
        ((UINT64)(*Buffer) > (HIGHER_HALF_KERNEL_HEAP + (PageCapacity * PHYSICAL_MEMORY_PAGE_SIZE) - sizeof(*HeapBlock))))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    HeapBlock = (HEAP_BLOCK *)((UINT64)(*Buffer) - sizeof(*HeapBlock));
    if ((HEAP_MAGIC_ALLOCATED != HeapBlock->Magic) || (0 != (HeapBlock->Flags & FREE_BLOCK_MASK)))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    HeapBlock->Magic = HEAP_MAGIC_FREE;
    HeapBlock->Flags &= (~FREE_BLOCK_MASK);
    *Buffer = NULL_PTR;

    if ((NULL_PTR != HeapBlock->Next) && (HEAP_MAGIC_FREE == HeapBlock->Next->Magic) && (0 != (HeapBlock->Next->Flags & FREE_BLOCK_MASK)))
    {
        HeapBlock->Size += (HeapBlock->Next->Size + sizeof(*HeapBlock));
        if (NULL_PTR != HeapBlock->Next->Next)
        {
            HeapBlock->Next->Next->Previous = HeapBlock;
        }
        HeapBlock->Next = HeapBlock->Next->Next;
    }

    if ((NULL_PTR != HeapBlock->Previous) && (HEAP_MAGIC_FREE == HeapBlock->Previous->Magic) && (0 != (HeapBlock->Previous->Flags & FREE_BLOCK_MASK)))
    {
        HeapBlock->Previous->Size += (HeapBlock->Size + sizeof(*HeapBlock));
        if (NULL_PTR != HeapBlock->Next)
        {
            HeapBlock->Next->Previous = HeapBlock->Previous;
        }
        HeapBlock->Previous->Next = HeapBlock->Next;
        HeapBlock = HeapBlock->Previous;
    }

    if (NULL_PTR == HeapBlock->Next)
    {
        Status = DecreraseAllocatedHeap(HeapBlock);
        if (E_OK != Status)
        {
            goto Cleanup;
        }
    }

Cleanup:
    return Status;
}

static inline UINT64 TagFromChar16(const CHAR16 Tag[4])
{
    return ((UINT64)Tag[0] << 0)  |
           ((UINT64)Tag[1] << 16) |
           ((UINT64)Tag[2] << 32) |
           ((UINT64)Tag[3] << 48);
}

static STATUS API IncreaseAllocatedHeap(HEAP_BLOCK *LastHeapBlock, UINT64 Size)
{
    STATUS Status = E_OK;
    UINT64 PhysicalAddress = 0;
    UINT64 NeededPages = 0;
    UINT64 AllocatedPages = 0;

    if (NULL_PTR == LastHeapBlock)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if ((HEAP_MAGIC_FREE == LastHeapBlock->Magic) && (0 != (LastHeapBlock->Flags & FREE_BLOCK_MASK)) && (LastHeapBlock->Size >= Size))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if ((HEAP_MAGIC_FREE == LastHeapBlock->Magic) && (0 != (LastHeapBlock->Flags & FREE_BLOCK_MASK)))
    {
        NeededPages = ALIGN_UP((Size - LastHeapBlock->Size ), PHYSICAL_MEMORY_PAGE_SIZE) / PHYSICAL_MEMORY_PAGE_SIZE;
    }
    else
    {
        NeededPages = ALIGN_UP((Size + sizeof(*LastHeapBlock)), PHYSICAL_MEMORY_PAGE_SIZE) / PHYSICAL_MEMORY_PAGE_SIZE;
    }

    AllocatedPages = (1ULL << (CeilLog2(NeededPages)));
    if (AllocatedPages < HEAP_PHYSICAL_ALLOCATION_PAGE_COUNT)
    {
        AllocatedPages = HEAP_PHYSICAL_ALLOCATION_PAGE_COUNT;
    }

    if (HIGHER_HALF_KERNEL_HEAP + (PageCapacity + AllocatedPages) * PHYSICAL_MEMORY_PAGE_SIZE >= HIGHER_HALF_KERNEL_HEAP_LIMIT)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Status = SystemPhysicalMemoryAllocatePages(&PhysicalAddress, AllocatedPages);
    if (E_OK != Status)
    {
        goto Cleanup;
    }

    Status = SystemVirtualMemoryMapPages
    (
        &KernelMemorySpace,
        HIGHER_HALF_KERNEL_HEAP + (PageCapacity * PHYSICAL_MEMORY_PAGE_SIZE),
        PhysicalAddress,
        AllocatedPages,
        SYSTEM_VIRTUAL_MEMORY_FLAG_READ_WRITE | SYSTEM_VIRTUAL_MEMORY_FLAG_GLOBAL_ENABLED,
        MemoryCacheWriteBack
    );
    if (E_OK != Status)
    {
        goto Cleanup;
    }

    PageCapacity += AllocatedPages;
    if ((HEAP_MAGIC_ALLOCATED == LastHeapBlock->Magic) && (0 == (LastHeapBlock->Flags & FREE_BLOCK_MASK)))
    {
        HEAP_BLOCK *AllocatedHeap = (HEAP_BLOCK *)((UINT64)LastHeapBlock + sizeof(*LastHeapBlock) + LastHeapBlock->Size);
        AllocatedHeap->Magic = HEAP_MAGIC_FREE;
        AllocatedHeap->Flags |= FREE_BLOCK_MASK;
        AllocatedHeap->Size = (AllocatedPages * PHYSICAL_MEMORY_PAGE_SIZE) - sizeof(*AllocatedHeap);
        AllocatedHeap->Next = NULL_PTR;
        AllocatedHeap->Previous = LastHeapBlock;
        AllocatedHeap->AllocaterTag = TagFromChar16(u"HEAP");
        LastHeapBlock->Next = AllocatedHeap;
    }
    else
    {
        LastHeapBlock->Size += (AllocatedPages * PHYSICAL_MEMORY_PAGE_SIZE);
    }
Cleanup:
    if (E_OK != Status)
    {
        if (0 != PhysicalAddress)
        {
            SystemPhysicalMemoryFreePages(&PhysicalAddress);
        }
    }

    return Status;
}

static STATUS API DecreraseAllocatedHeap(HEAP_BLOCK *LastHeapBlock)
{
    STATUS Status = E_OK;
    UINT64 LastHeapBlockByteOffset = 0;
    UINT64 DeallocatedChunks = 0;

    if (NULL_PTR == LastHeapBlock)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if ((HEAP_MAGIC_FREE != LastHeapBlock) || (0 == (LastHeapBlock->Flags & FREE_BLOCK_MASK)))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

Cleanup:
    return Status;
}
