#include "xsdt_parser.h"

#include "system_log.h"
#include "../sdth_parser/sdth_parser.h"
#include "../madt_parser/madt_parser.h"
#include "../mcfg_parser/mcfg_parser.h"
#include "utils.h"

#define MODULE_TAG u"XSDT"

#define MADT_SIGNATURE "APIC"
#define MCFG_SIGNATURE "MCFG"

typedef struct _EXTENDED_SYSTEM_DESCRIPTION_TABLE
{
    SYSTEM_DESCRIPTION_TABLE_HEADER Header;
    UINT64 Entries[];
} PACKED EXTENDED_SYSTEM_DESCRIPTION_TABLE;

STATUS API XsdtFillSystemConfigurationTable(CONST UINT64 ExtendedSystemDescriptionTable, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration)
{
    CHAR16 Signature[5];
    SYSTEM_DESCRIPTION_TABLE_HEADER *Sdth = NULL_PTR;
    EXTENDED_SYSTEM_DESCRIPTION_TABLE *Xsdt = (EXTENDED_SYSTEM_DESCRIPTION_TABLE *)PhysicalToVirtual(ExtendedSystemDescriptionTable);
    SdthFillSystemConfigurationTable((UINT64)&Xsdt->Header, SystemConfiguration);

    UINT64 EntryIndex = 0;
    for (UINT64 i = sizeof(*Xsdt); i < Xsdt->Header.Length; i += sizeof(*Xsdt->Entries))
    {
        Sdth = (SYSTEM_DESCRIPTION_TABLE_HEADER *)PhysicalToVirtual(Xsdt->Entries[EntryIndex]);
        Char8ToChar16(Sdth->Signature, Signature, 4);

        if (0 == MemoryCompare(Sdth->Signature, MADT_SIGNATURE, 4))
        {
            MadtFillSystemConfigurationTable(Xsdt->Entries[EntryIndex], SystemConfiguration);
        }
        else if (0 == MemoryCompare(Sdth->Signature, MCFG_SIGNATURE, 4))
        {
            McfgFillSystemConfigurationTable(Xsdt->Entries[EntryIndex], SystemConfiguration);
        }
        else
        {
            LOG_DEBUG(u"Unkown Signature: %s", Signature);
        }

        ++EntryIndex;
    }

    return E_OK;
}
