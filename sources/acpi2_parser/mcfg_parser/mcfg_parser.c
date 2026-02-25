#include "mcfg_parser.h"

#include "../sdth_parser/sdth_parser.h"
#include "system_log.h"

#define MODULE_TAG u"MCFG"

typedef struct _MEMORY_MAPPED_CONFIGURATION_ENTRY
{
    UINT64 BaseAddress;
    UINT16 PciSegmentGroupNumber;
    UINT8 StartPciNumber;
    UINT8 EndPciNumber;
    UINT32 Reserved;
} PACKED MEMORY_MAPPED_CONFIGURATION_ENTRY;

typedef struct _MEMORY_MAPPED_CONFIGURATION
{
    SYSTEM_DESCRIPTION_TABLE_HEADER Header;
    UINT64 Reserved;
    MEMORY_MAPPED_CONFIGURATION_ENTRY Entries[];
} PACKED MEMORY_MAPPED_CONFIGURATION;

STATUS API McfgFillSystemConfigurationTable(IN CONST UINT64 MemoryMappedConfiguration,
                                            OUT SYSTEM_CONFIGURATION_TABLE *SystemConfigurationTable)
{

    MEMORY_MAPPED_CONFIGURATION *Mcfg = (MEMORY_MAPPED_CONFIGURATION *)PhysicalToVirtual(MemoryMappedConfiguration);

    SdthFillSystemConfigurationTable((UINT64)&Mcfg->Header, SystemConfigurationTable);

    UINT64 EntryIndex = 0;
    for (UINT64 i = sizeof(*Mcfg); i < Mcfg->Header.Length; i += sizeof(*Mcfg->Entries))
    {
        LOG_INFO(u"Base Address: 0x%x", Mcfg->Entries[EntryIndex].BaseAddress);
        LOG_INFO(u"Pci Segment Group Number: 0x%x", Mcfg->Entries[EntryIndex].PciSegmentGroupNumber);
        LOG_INFO(u"Start Pci Number: 0x%x", Mcfg->Entries[EntryIndex].StartPciNumber);
        LOG_INFO(u"End Pci Number: 0x%x", Mcfg->Entries[EntryIndex].EndPciNumber);

        SystemConfigurationTable->MemoryMappedConfigurationBaseAddress = Mcfg->Entries[EntryIndex].BaseAddress;
        ++EntryIndex;
    }

    return E_OK;
}
