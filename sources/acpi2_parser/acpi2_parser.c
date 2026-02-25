#include "acpi2_parser.h"

#include "xsdt_parser/xsdt_parser.h"
#include "utils.h"
#include "system_log.h"

#define MODULE_TAG u"ACPI2"

typedef struct _ROOT_SYSTEM_DESCRIPTION_POINTER
{
    CHAR8 Signature[8];
    UINT8 Checksum;
    CHAR8 OemId[6];
    UINT8 Revision;
    UINT32 RsdtAddress;
    UINT32 Length;
    UINT64 XsdtAddress;
    UINT8 ExtendedChecksum;
    UINT8 Reserved[3];
} PACKED ROOT_SYSTEM_DESCRIPTION_POINTER;

extern KERNEL_BOOT_INFORMATION KernelBootInformation;

static STATUS API ParseRootSystemDescriptor(IN CONST ROOT_SYSTEM_DESCRIPTION_POINTER *RootSystemDescriptonPointer,
                                            OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration);

STATUS API Acpi2FillSystemConfigurationTable(IN CONST UINT64 RootSystemDescriptorPointer, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfigurationTable)
{
    ROOT_SYSTEM_DESCRIPTION_POINTER *Rsdp = (ROOT_SYSTEM_DESCRIPTION_POINTER *)PhysicalToVirtual(RootSystemDescriptorPointer);
    ParseRootSystemDescriptor(Rsdp, SystemConfigurationTable);
    return E_OK;
}

static STATUS API ParseRootSystemDescriptor(IN CONST ROOT_SYSTEM_DESCRIPTION_POINTER *RootSystemDescriptonPointer,
                                            OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration)
{
    CHAR16 Signature[9];
    CHAR16 OemId[7];
    Char8ToChar16(RootSystemDescriptonPointer->Signature, Signature, 8);
    Char8ToChar16(RootSystemDescriptonPointer->OemId, OemId, 6);

    LOG_DEBUG(u"Signature: %s", Signature);
    LOG_DEBUG(u"Checksum: 0x%x", RootSystemDescriptonPointer->Checksum);
    LOG_DEBUG(u"Oem Id: %s", OemId);
    LOG_DEBUG(u"Revision: 0x%x", RootSystemDescriptonPointer->Revision);
    LOG_DEBUG(u"Rsdt Address: 0x%x", RootSystemDescriptonPointer->RsdtAddress);
    LOG_DEBUG(u"Length: 0x%x", RootSystemDescriptonPointer->Length);
    LOG_DEBUG(u"Xsdt Address: 0x%x", RootSystemDescriptonPointer->XsdtAddress);
    LOG_DEBUG(u"Extended Checksum: 0x%x", RootSystemDescriptonPointer->ExtendedChecksum);

    XsdtFillSystemConfigurationTable(RootSystemDescriptonPointer->XsdtAddress, SystemConfiguration);
    return E_OK;
}
