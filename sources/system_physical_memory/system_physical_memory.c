#include "system_physical_memory.h"

#include "utils.h"

#define PAGE_SIZE 4096

#define HIGHEST_ORDER 20

#define PAGE_FRAME_METADATA_RESERVED_FLAG (1ULL << 0ULL)
#define PAGE_FRAME_METADATA_USED_FLAG (1ULL << 1ULL)

typedef struct _PAGE_FRAME_METADATA PAGE_FRAME_METADATA;

typedef struct _PAGE_FRAME_METADATA
{
    UINT32 ReferenceCount;
    PAGE_FRAME_METADATA *Next;
    UINT8 Order;
    UINT8 Flags;
    UINT16 Reserved1;
    UINT32 Rfu;
} PAGE_FRAME_METADATA;

static PAGE_FRAME_METADATA* PageFrameMetadata = NULL_PTR;
static UINT64 TotalNumberOfPages = 0;

static PAGE_FRAME_METADATA *OrderFreeList[HIGHEST_ORDER] = { NULL_PTR };

extern KERNEL_BOOT_INFORMATION KernelBootInformation;

static VOID API InsertOrder(UINT64 PageIndex, UINT8 Order);
static VOID API ProcessFreeRun(UINT64 FirstFreePage, UINT64 PageCount);
static VOID API RemoveFreeBlock(UINT64 PageIndex, UINT8 Order);
static VOID API MergeFreeBlock(UINT64 PageIndex);

STATUS API SystemPhysicalMemoryInit(IN SYSTEM_MEMORY *SystemMemory)
{
    STATUS Status = E_OK;
    UINT64 PageFrameMetadataNumberOfPages = 0;
    UINT64 PageFrameMetadataPhysicalAddress = 0;

    if (NULL_PTR == SystemMemory)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    for (UINT64 OrderIndex = 0; OrderIndex < HIGHEST_ORDER; ++OrderIndex)
    {
        OrderFreeList[OrderIndex] = NULL_PTR;
    }

    for (UINT64 MemoryDescriptorIndex = 0; MemoryDescriptorIndex < SystemMemory->NumberOfMemoryDescriptors; ++MemoryDescriptorIndex)
    {
        MEMORY_REGION_DESCRIPTOR *MemoryDescriptor = &SystemMemory->MemoryDescriptors[MemoryDescriptorIndex];
        if (TotalNumberOfPages < ((MemoryDescriptor->PhysicalStart / PAGE_SIZE) + MemoryDescriptor->PageCount))
        {
            TotalNumberOfPages = ((MemoryDescriptor->PhysicalStart / PAGE_SIZE) + MemoryDescriptor->PageCount);
        }
    }

    PageFrameMetadataNumberOfPages = (TotalNumberOfPages * sizeof(PAGE_FRAME_METADATA) + PAGE_SIZE - 1) / PAGE_SIZE;
    for (UINT64 MemoryDescriptorIndex = 0; (MemoryDescriptorIndex < SystemMemory->NumberOfMemoryDescriptors) && (0 == PageFrameMetadataPhysicalAddress); ++MemoryDescriptorIndex)
    {
        MEMORY_REGION_DESCRIPTOR *MemoryDescriptor = &SystemMemory->MemoryDescriptors[MemoryDescriptorIndex];
        if (MemoryDescriptor->Type == MemoryUsable)
        {
            if ((PageFrameMetadataNumberOfPages <= MemoryDescriptor->PageCount) && (0 != MemoryDescriptor->PhysicalStart))
            {
                PageFrameMetadataPhysicalAddress = MemoryDescriptor->PhysicalStart;
            }
        }
    }

    if (0 == PageFrameMetadataPhysicalAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    PageFrameMetadata = (PAGE_FRAME_METADATA *)KernelBootInformation.PhysicalToVirtualMap(PageFrameMetadataPhysicalAddress);
    MemorySet(PageFrameMetadata, 0, PageFrameMetadataNumberOfPages * PAGE_SIZE);
    for (UINT64 PageIndex = 0 ; PageIndex < TotalNumberOfPages ; PageIndex ++)
    {
        PageFrameMetadata[PageIndex].Flags |= PAGE_FRAME_METADATA_RESERVED_FLAG;
    }

    for (UINT64 MemoryDescriptorIndex = 0; MemoryDescriptorIndex < SystemMemory->NumberOfMemoryDescriptors; ++MemoryDescriptorIndex)
    {
        MEMORY_REGION_DESCRIPTOR *MemoryDescriptor = &SystemMemory->MemoryDescriptors[MemoryDescriptorIndex];
        if ((MemoryDescriptor->Type == MemoryUsable) && (MemoryDescriptor->PageCount > 0))
        {
            UINT64 FirstPageIndex = MemoryDescriptor->PhysicalStart / PAGE_SIZE;
            for (UINT64 PageIndex = 0; PageIndex < MemoryDescriptor->PageCount; ++PageIndex)
            {
                PageFrameMetadata[FirstPageIndex + PageIndex].Flags &= (~PAGE_FRAME_METADATA_RESERVED_FLAG);
            }
        }
    }

    /* Mark Page Frame Information as reserved */
    UINT64 FirstPageFrameMetadata = (PageFrameMetadataPhysicalAddress / PAGE_SIZE);
    for (UINT64 PageIndex =  0; PageIndex < PageFrameMetadataNumberOfPages ; PageIndex ++)
    {
        PageFrameMetadata[FirstPageFrameMetadata + PageIndex].Flags |= PAGE_FRAME_METADATA_RESERVED_FLAG;
    }

    /* Mark Zero Page as reserved (NULL_PTR protection) */
    PageFrameMetadata[0].Flags |= PAGE_FRAME_METADATA_RESERVED_FLAG;

    /* Process Page Frame Metadata Order arrays */
    UINT64 FirstFreePage = 0;
    UINT64 LastFreePage = 0;
    BOOLEAN IsFreeRun = FALSE;
    for (UINT64 PageIndex = 0 ; PageIndex < TotalNumberOfPages ; PageIndex ++)
    {
        BOOLEAN IsPageFree = ((PageFrameMetadata[PageIndex].Flags & PAGE_FRAME_METADATA_RESERVED_FLAG) == 0ULL);
        if (TRUE == IsPageFree)
        {
            if (FALSE == IsFreeRun)
            {
                IsFreeRun = TRUE;
                FirstFreePage = PageIndex;
            }
            LastFreePage = PageIndex;
        }
        else
        {
            if (TRUE == IsFreeRun)
            {
                ProcessFreeRun(FirstFreePage, LastFreePage - FirstFreePage + 1);
                IsFreeRun = FALSE;
            }
        }
    }

    if (TRUE == IsFreeRun)
    {
        ProcessFreeRun(FirstFreePage, LastFreePage - FirstFreePage + 1);
    }

Cleanup:
    return Status;
}

STATUS API SystemPhysicalMemoryAllocatePages(OUT UINT64 *PhysicalAddress, IN CONST UINT64 PageCount)
{
    STATUS Status = E_OK;
    UINT64 PhysicalAddressResult = 0;
    UINT64 PageIndex = 0;
    UINT8 AllocatedOrder = 0;
    UINT8 FoundOrder = 0;
    PAGE_FRAME_METADATA *PageIndexFrameMetadata = NULL_PTR;

    if (NULL_PTR == PhysicalAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0ULL != *PhysicalAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0ULL == PageCount)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    AllocatedOrder = CeilLog2(PageCount);
    FoundOrder = AllocatedOrder;

    if (AllocatedOrder >= HIGHEST_ORDER)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    while ((FoundOrder < HIGHEST_ORDER) && (NULL_PTR == OrderFreeList[FoundOrder]))
    {
        FoundOrder++;
    }

    if (HIGHEST_ORDER == FoundOrder)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    /* Remove from Order Free List */
    PageIndexFrameMetadata = OrderFreeList[FoundOrder];
    OrderFreeList[FoundOrder] = OrderFreeList[FoundOrder]->Next;
    PageIndexFrameMetadata->Next = NULL_PTR;
    PageIndex = (UINT64)(PageIndexFrameMetadata - PageFrameMetadata);

    /* Add remaning memory back into the Order Free List */
    while (FoundOrder > AllocatedOrder)
    {
        FoundOrder --;
        InsertOrder(PageIndex + (1ULL << FoundOrder), FoundOrder);
    }

    PageIndexFrameMetadata->Flags |= PAGE_FRAME_METADATA_USED_FLAG;
    PageIndexFrameMetadata->Order = AllocatedOrder;
    PageIndexFrameMetadata->ReferenceCount = 1;

    PhysicalAddressResult = PageIndex * PAGE_SIZE;
    *PhysicalAddress = PhysicalAddressResult;

Cleanup:
    if (E_OK != Status)
    {
        *PhysicalAddress = 0;
    }
    return Status;
}

STATUS API SystemPhysicalMemoryFreePages(IN OUT UINT64 *PhysicalAddress)
{
    STATUS Status = E_OK;
    UINT64 PageIndex = 0;

    if (NULL_PTR == PhysicalAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 == *PhysicalAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 != ((*PhysicalAddress) % PAGE_SIZE))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    PageIndex = (*PhysicalAddress) / PAGE_SIZE;
    if (PageIndex >= TotalNumberOfPages)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 == (PageFrameMetadata[PageIndex].Flags & PAGE_FRAME_METADATA_USED_FLAG))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 != (*PhysicalAddress) % ((1ULL << PageFrameMetadata[PageIndex].Order) * PAGE_SIZE))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    PageFrameMetadata[PageIndex].ReferenceCount--;
    if (0 == PageFrameMetadata[PageIndex].ReferenceCount)
    {
        PageFrameMetadata[PageIndex].Flags &= (~PAGE_FRAME_METADATA_USED_FLAG);
        MemorySet((VOID *)KernelBootInformation.PhysicalToVirtualMap(PageIndex * PAGE_SIZE), 
                    0U, 
                    (1ULL << PageFrameMetadata[PageIndex].Order) * PAGE_SIZE);
        MergeFreeBlock(PageIndex);
    }

    *PhysicalAddress = 0;
Cleanup:
    return Status;
}

STATUS API SystemPhysicalMemoryIncreaseReferenceCount(UINT64 PhysicalAddress)
{
    STATUS Status = E_OK;
    UINT64 PageIndex = PhysicalAddress / PAGE_SIZE;

    if (0 == PhysicalAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 != (PhysicalAddress % PAGE_SIZE))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (PageIndex >= TotalNumberOfPages)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 == (PageFrameMetadata[PageIndex].Flags & PAGE_FRAME_METADATA_USED_FLAG))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (0 != PhysicalAddress % ((1ULL << PageFrameMetadata[PageIndex].Order) * PAGE_SIZE))
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    PageFrameMetadata[PageIndex].ReferenceCount++;

Cleanup:
    return Status;
}

STATUS API SystemPhysicalMemoryAllocatePool(OUT VOID **Buffer, IN CONST UINT64 Size)
{
    STATUS Status = E_OK;
    UINT64 PhysicalAddress = 0;

    Status = SystemPhysicalMemoryAllocatePages(&PhysicalAddress, (Size + PAGE_SIZE - 1) / PAGE_SIZE);
    if (E_OK == Status)
    {
        *Buffer = (VOID *)PhysicalAddress;
    }
    else
    {
        *Buffer = NULL_PTR;
    }

    return Status;
}

STATUS API SystemPhysicalMemoryFreePool(IN OUT VOID **Buffer, IN CONST UINT64 Size)
{
    (void) Size;
    return SystemPhysicalMemoryFreePages((UINT64 *)Buffer);
}

static VOID API InsertOrder(UINT64 PageIndex, UINT8 Order)
{
    PAGE_FRAME_METADATA *PageIndexFrameMetadata = &PageFrameMetadata[PageIndex];
    PageIndexFrameMetadata->Order = Order;
    PageIndexFrameMetadata->Next = OrderFreeList[Order];
    OrderFreeList[Order] = PageIndexFrameMetadata;
}

static VOID API ProcessFreeRun(UINT64 FirstFreePage, UINT64 PageCount)
{
    UINT64 MaximumAlignment = 0;
    UINT64 SizeLimit = 0;
    UINT8 Order = 0;
    UINT64 OrderNumberOfBlocks = 0;

    while (PageCount > 0)
    {
        MaximumAlignment = CountTrailingZeros(FirstFreePage);
        SizeLimit = FloorLog2(PageCount);
        Order = MaximumAlignment;
        if (Order > SizeLimit)
        {
            Order = SizeLimit;
        }
        if (Order >= HIGHEST_ORDER)
        {
            Order = HIGHEST_ORDER - 1;
        }

        OrderNumberOfBlocks = (1ULL << Order);
        InsertOrder(FirstFreePage, Order);
        FirstFreePage += OrderNumberOfBlocks;
        PageCount -= OrderNumberOfBlocks;
    }
}

static VOID API RemoveFreeBlock(UINT64 PageIndex, UINT8 Order)
{
    PAGE_FRAME_METADATA *PageIndexFrameMetadata = OrderFreeList[Order];
    if (NULL_PTR == PageIndexFrameMetadata)
    {
        return;
    }

    if (PageIndex == (UINT64)(PageIndexFrameMetadata - PageFrameMetadata))
    {
        OrderFreeList[Order] = PageIndexFrameMetadata->Next;
        PageIndexFrameMetadata->Next = NULL_PTR;
        return;
    }

    while ((NULL_PTR != PageIndexFrameMetadata->Next) && (PageIndex != (UINT64)(PageIndexFrameMetadata->Next - PageFrameMetadata)))
    {
        PageIndexFrameMetadata = PageIndexFrameMetadata->Next;
    }

    if (NULL_PTR == PageIndexFrameMetadata->Next)
    {
        return;
    }

    PAGE_FRAME_METADATA *RemovedPageFrame = PageIndexFrameMetadata->Next;
    PageIndexFrameMetadata->Next = PageIndexFrameMetadata->Next->Next;
    RemovedPageFrame->Next = NULL_PTR;
    RemovedPageFrame->Order = 0;
    RemovedPageFrame->ReferenceCount = 0;
}

static BOOLEAN IsPageFree(UINT64 PageIndex)
{
    return (PageIndex < TotalNumberOfPages) && 
    (0 == ((PAGE_FRAME_METADATA_RESERVED_FLAG | PAGE_FRAME_METADATA_USED_FLAG) & PageFrameMetadata[PageIndex].Flags)) &&
    (0 == PageFrameMetadata[PageIndex].ReferenceCount);
}

static VOID API MergeFreeBlock(UINT64 PageIndex)
{
    if (FALSE == IsPageFree(PageIndex))
    {
        return;
    }

    PAGE_FRAME_METADATA *PageIndexFrameMetadata = &(PageFrameMetadata[PageIndex]);
    UINT8 CurrentOrder = PageIndexFrameMetadata->Order;
    UINT64 BuddyIndex = PageIndex ^ (1ULL << CurrentOrder);

    while ((CurrentOrder < (HIGHEST_ORDER - 1)) && (IsPageFree(BuddyIndex) && (CurrentOrder == PageFrameMetadata[BuddyIndex].Order)))
    {
        RemoveFreeBlock(BuddyIndex, CurrentOrder);

        PageIndex &= (~(1ULL << CurrentOrder));
        CurrentOrder++;
        BuddyIndex = PageIndex ^ (1ULL << CurrentOrder);
    }

    InsertOrder(PageIndex, CurrentOrder);
}
