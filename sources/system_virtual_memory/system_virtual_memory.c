#include "system_virtual_memory.h"

#include "utils.h"
#include "system_physical_memory.h"
#include "msr.h"

#define PAGE_SIZE 4096

#define USER_IMAGE_VIRTUAL_BASE_ADDRESS         0x0000000000400000
#define USER_MEMMAP_VIRTUAL_BASE_ADDRESS        0x0000100000000000
#define USER_STACK_VIRTUAL_UPPER_BOUND_ADDRESS  0x00070FFFFFFFF000

#define HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS         0xFFFF800000000000
#define HIGHER_HALF_KERNEL_HEAP_BASE_VIRTUAL_ADDRESS        0xFFFFFFF800000000
#define HIGHER_HALF_PER_CPU_DATA_VIRTUAL_BASE_ADDRESS       0xFFFFFFFF00000000
#define HIGHER_HALF_KERNEL_IMAGE_VIRTUAL_BASE_ADDRESS       0xFFFFFFFF80000000
#define HIGHER_HALF_TEMPORARY_MAPPING_VIRTUAL_BASE_ADDRESS  0xFFFFFFFFFF000000

#define PAGE_ATTRIBUTE_DECODING_TABLE_MSR 0x277

#define PRESENT_MASK (1ULL << 0ULL)
#define READ_WRITE_MASK (1ULL << 1ULL)
#define USER_SUPERVISOR_MASK (1ULL << 2ULL)
#define PAGE_ATTRIBUTE_MASK (0x7ULL << 3ULL)
#define EXECUTE_DISABLE_MASK (1ULL << 63ULL)

typedef enum _PAGE_ATTRIBUTE
{
    PageAttributeUncacheable        = 0,
    PageAttributeWriteCombining     = 1,
    PageAttributeWriteThrough       = 4,
    PageAttributeWriteProtected     = 5,
    PageAttributeWriteBack          = 6,
    PageAttributeUncacheableMinus   = 7
} PAGE_ATTRIBUTE;

typedef UINT64 (*GET_VIRTUAL_ADDRESS)(UINT64 PhysicalAddress);

GET_VIRTUAL_ADDRESS GetVirtualAddress = IdentityMap;

static UINT64 PageMapLevel4PhysicalAddress = 0;
static UINT64* PageMapLevel4 = NULL_PTR;

static UINT8 PageAttributeLookup[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static VOID API MapPage
(
    IN UINT64 VirtualAddress, 
    IN UINT64 PhysicalAddress,
    IN BOOLEAN ReadWrite,
    IN BOOLEAN UserSupervisor,
    IN BOOLEAN ExecuteDisable,
    IN UINT8 PageAttributeDecodingTableIndex
);

static UINT64 API IdentityMap(UINT64 PhysicalAddress);

static UINT64 API HigherHalfMap(UINT64 PhysicalAddress);

static VOID API SetPageAttributeLookupTable(VOID);

static VOID API JumpToHigherHalf(IN UINT64 KernelVirtualBaseAddress, IN UINT64 KernelPhysicalBaseAddress);

STATUS API SystemVirtualMemoryInit(IN UINT64 KernelPhysicalBaseAddress, IN UINT64 KernelVirtualBaseAddress)
{
    STATUS Status = E_OK;
    UINT64 MemoryMapPhysicalAddress = NULL_PTR;
    UINT8 *MemoryMap = NULL_PTR;
    UINT64 MemoryMapSize = 0;
    UINT64 TotalRamSize = 0;

    Status = SystemPhysicalMemoryGetCurrentMemoryMap(&MemoryMapPhysicalAddress, &MemoryMapSize);
    if (E_OK != Status)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    MemoryMap = (UINT8 *)GetVirtualAddress(MemoryMapPhysicalAddress);

    TotalRamSize = MemoryMapSize * sizeof(*MemoryMap) * PAGE_SIZE;
    if (KernelVirtualBaseAddress < TotalRamSize)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    SetPageAttributeLookupTable();
    if (PageAttributeLookup[PageAttributeWriteCombining] == 0xFF || PageAttributeLookup[PageAttributeWriteThrough] == 0xFF || 
        PageAttributeLookup[PageAttributeWriteProtected] == 0xFF || PageAttributeLookup[PageAttributeWriteBack] == 0xFF || 
        PageAttributeLookup[PageAttributeUncacheable] == 0xFF || PageAttributeLookup[PageAttributeUncacheableMinus] == 0xFF)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Status = SystemPhysicalMemoryAllocatePages(&PageMapLevel4PhysicalAddress, 1);
    if (E_OK != Status)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
    PageMapLevel4 = (UINT64 *)GetVirtualAddress(PageMapLevel4PhysicalAddress);
    MemorySet(PageMapLevel4, 0, PAGE_SIZE);

    /* Higher Half Direct Map */
    for (UINT64 PageIndex = 0; PageIndex < (TotalRamSize / PAGE_SIZE); ++PageIndex)
    {
        MapPage(HigherHalfMap(PageIndex * PAGE_SIZE), PageIndex * PAGE_SIZE, TRUE, FALSE, FALSE, PageAttributeWriteBack);
    }

    ASM("mov cr3, %0" : : "r"(PageMapLevel4PhysicalAddress));
    GetVirtualAddress = HigherHalfMap;
    JumpToHigherHalf(KernelVirtualBaseAddress, KernelPhysicalBaseAddress);
    Cleanup:
    if (E_NOT_OK == Status)
    {
        if (0 != PageMapLevel4PhysicalAddress)
        {
            SystemPhysicalMemoryFreePages(&PageMapLevel4PhysicalAddress, 1);
        }
    }
    return Status;
}

STATUS API SystemVirtualMemoryAllocate
(
    OUT UINT64 *VirtualAddress,
    IN CONST UINT64 NumberOfPages,
    IN BOOLEAN ReadWrite,
    IN BOOLEAN UserSupervisor,
    IN BOOLEAN ExecuteDisable,
    IN PAGE_ATTRIBUTE PageAttribute
)
{
    STATUS Status = E_OK;
    UINT64 PhysicalAddress = 0;
    UINT64 VirtualAddressResult = 0;
    BOOLEAN FoundUsedPage = FALSE;

    if (NULL_PTR == VirtualAddress)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Status = SystemPhysicalMemoryAllocatePages(&PhysicalAddress, 1);
    if (E_OK != Status)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    VirtualAddressResult = PhysicalAddress + HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS;

    MapPage(VirtualAddressResult, PhysicalAddress, ReadWrite, UserSupervisor, ExecuteDisable, PageAttribute);

    Cleanup:
    *VirtualAddress = VirtualAddressResult;
    return Status;
}

static UINT64 API IdentityMap(UINT64 PhysicalAddress)
{
    return PhysicalAddress;
}

static UINT64 API HigherHalfMap(UINT64 PhysicalAddress)
{
    return PhysicalAddress + HIGHER_HALF_DIRECT_MAP_VIRTUAL_BASE_ADDRESS;
}

static VOID API MapPage
(
    IN UINT64 VirtualAddress, 
    IN UINT64 PhysicalAddress,
    IN BOOLEAN ReadWrite,
    IN BOOLEAN UserSupervisor,
    IN BOOLEAN ExecuteDisable,
    IN UINT8 PageAttribute
)
{
    UINT64 PageDirectoryPointerTablePhysicalAddress = 0;
    UINT64 PageDirectoryPhysicalAddress = 0;
    UINT64 PageTablePhysicalAddress = 0;

    UINT8 PageMapLevel4Index = (VirtualAddress >> 39) & 0x1FF;
    UINT8 PageDirectoryPointerTableIndex = (VirtualAddress >> 30) & 0x1FF;
    UINT8 PageDirectoryIndex = (VirtualAddress >> 21) & 0x1FF;
    UINT8 PageTableIndex = (VirtualAddress >> 12) & 0x1FF;

    UINT64 *PageDirectoryPointerTable = NULL_PTR;
    UINT64 *PageDirectory = NULL_PTR;
    UINT64 *PageTable = NULL_PTR;

    if (0ULL == (PageMapLevel4[PageMapLevel4Index] & 0x1ULL))
    {
        SystemPhysicalMemoryAllocatePages(&PageDirectoryPointerTablePhysicalAddress, 1);
        PageMapLevel4[PageMapLevel4Index] = PageDirectoryPointerTablePhysicalAddress | 0x1FULL;
        MemorySet((VOID *)GetVirtualAddress(PageDirectoryPointerTablePhysicalAddress), 0, PAGE_SIZE);
    }

    PageDirectoryPointerTable = (UINT64 *)GetVirtualAddress(PageMapLevel4[PageMapLevel4Index] & 0xFFFFFFFFF000ULL);
    if (0ULL == (PageDirectoryPointerTable[PageDirectoryPointerTableIndex] & 0x1ULL))
    {
        SystemPhysicalMemoryAllocatePages(&PageDirectoryPhysicalAddress, 1);
        PageDirectoryPointerTable[PageDirectoryPointerTableIndex] = PageDirectoryPhysicalAddress | 0x1FULL;
        MemorySet((VOID *)GetVirtualAddress(PageDirectoryPhysicalAddress), 0, PAGE_SIZE);
    }

    PageDirectory = (UINT64 *)GetVirtualAddress(PageDirectoryPointerTable[PageDirectoryPointerTableIndex] & 0xFFFFFFFFF000ULL);
    if (0ULL == (PageDirectory[PageDirectoryIndex] & 0x1ULL))
    {
        SystemPhysicalMemoryAllocatePages(&PageTablePhysicalAddress, 1);
        PageDirectory[PageDirectoryIndex] = PageTablePhysicalAddress | 0x1FULL;
        MemorySet((VOID *)GetVirtualAddress(PageTablePhysicalAddress), 0, PAGE_SIZE);
    }

    PageTable = (UINT64 *)GetVirtualAddress(PageDirectory[PageDirectoryIndex] & 0xFFFFFFFFF000ULL);
    if (0ULL == (PageTable[PageTableIndex] & 0x1ULL))
    {
        PageTable[PageTableIndex] = PhysicalAddress & 0xFFFFFFFFF000;
        
        if (ReadWrite)
        {
            PageTable[PageTableIndex] |= READ_WRITE_MASK;
        }
        if (UserSupervisor)
        {            
            PageTable[PageTableIndex] |= USER_SUPERVISOR_MASK;
        }
        PageTable[PageTableIndex] |= (PageAttributeLookup[PageAttribute] & 0x7ULL) << 3;
        if (ExecuteDisable)
        {
            PageTable[PageTableIndex] |= EXECUTE_DISABLE_MASK;
        }
    }

}

static VOID API SetPageAttributeLookupTable(VOID)
{
    UINT64 SystemPageAttributeDecodingTableMsrValue = 0;
    ReadMsr(PAGE_ATTRIBUTE_DECODING_TABLE_MSR, &SystemPageAttributeDecodingTableMsrValue);

    for (UINT8 Index = 0; Index < 8; ++Index)
    {
        UINT8 Type = (SystemPageAttributeDecodingTableMsrValue >> (Index * 8)) & 0xFF;
        if ((Type == 0) || (Type == 1) || (Type == 4) || (Type == 5) || (Type == 6) || (Type == 7))
        {
            if (0xFFU == PageAttributeLookup[Type])
            {
                PageAttributeLookup[Type] = Index;
            }
        }
    }
}

static VOID API JumpToHigherHalf(IN UINT64 KernelVirtualBaseAddress, IN UINT64 KernelPhysicalBaseAddress)
{

    ASM("mov rax, %0" : : "r"(KernelVirtualBaseAddress));
    ASM("jmp rax");
}
