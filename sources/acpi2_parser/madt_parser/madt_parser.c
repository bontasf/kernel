#include "madt_parser.h"

#include "../sdth_parser/sdth_parser.h"
#include "system_log.h"

#define MODULE_TAG u"MADT"

typedef struct _MULTIPLE_APIC_DESCRIPTION_TABLE
{
    SYSTEM_DESCRIPTION_TABLE_HEADER Header;
    UINT32 LocalInterruptControllerAddress;
    union _MADT_FLAGS
    {
        struct _MADT_FIELDS
        {
            UINT32 PcAtCompatible : 1;
            UINT32 Reserved : 31;
        } Fields;
        UINT32 Value;
    } Flags;
} MULTIPLE_APIC_DESCRIPTION_TABLE;

typedef enum _INTERRUPT_CONTROLLER_STRUCTURE_TYPE
{
    EntryTypeLocalApic = 0x00,
    EntryTypeIoApic = 0x01,
    EntryTypeInterruptSourceOverride = 0x02,
    EntryTypeNmiSource = 0x03,
    EntryTypeLocalApicNmi = 0x04,
    EntryTypeLocalApicAddressOverride = 0x05,
} INTERRUPT_CONTROLLER_STRUCTURE_TYPE;

typedef struct _INTERRUPT_CONTROLLER_HEADER
{
    UINT8 Type;
    UINT8 Length;
} PACKED INTERRUPT_CONTROLLER_HEADER;

typedef struct _LOCAL_APIC
{
    UINT8 Type;
    UINT8 Length;
    UINT8 ProcessorUid;
    UINT8 ProcessorId;
    union _LOCAL_APIC_FLAGS
    {
        struct _LOCAL_APIC_FIELDS
        {
            UINT32 Enabled : 1;
            UINT32 OnlineCapabile : 1;
            UINT32 Reserved : 30;
        } Fields;
        UINT32 Value;
    } Flags;
} PACKED LOCAL_APIC;

typedef struct _IO_APIC
{
    UINT8 Type;
    UINT8 Length;
    UINT8 IoApicId;
    UINT8 Reserved;
    UINT32 IoApicAddress;
    UINT32 GlobalSystemInterruptBase;
} PACKED IO_APIC;

typedef struct _INTERRUPT_SOURCE_OVERRIDE
{
    UINT8 Type;
    UINT8 Length;
    UINT8 Bus;
    UINT8 Source;
    UINT32 GlobalSystemInterrupt;
    union __INTERRUPT_SOURCE_OVERRIDE_FLAGS
    {
        struct _INTERRUPT_SOURCE_OVERRICE_FIELDS
        {
            UINT16 Polarity : 2;
            UINT16 TriggerMode : 2;
            UINT16 Reserved : 12;
        } Fields;
        UINT16 Value;
    } Flags;
} PACKED INTERRUPT_SOURCE_OVERRIDE;

typedef struct _NMI_SOURCE
{
    UINT8 Type;
    UINT8 Length;
    union _NMI_SOURCE_FLAGS
    {
        struct _NMI_SOURCE_FIELDS
        {
            UINT16 Polarity : 2;
            UINT16 TriggerMode : 2;
            UINT16 Reserved : 12;
        } Fields;
        UINT16 Value;
    } Flags;
    UINT32 GlobalSystemInterrupt;
} PACKED NMI_SOURCE;

typedef struct _LOCAL_APIC_NMI
{
    UINT8 Type;
    UINT8 Length;
    UINT8 ProcessorUid;
    union _LOCAL_APIC_NMI_FLAGS
    {
        struct _LOCAL_APIC_NMI_FIELDS
        {
            UINT16 Polarity : 2;
            UINT16 TriggerMode : 2;
            UINT16 Reserved : 12;
        } Fields;
        UINT16 Value;
    } Flags;
    UINT8 LocalApicLintNumber;
} PACKED LOCAL_APIC_NMI;

typedef struct _LOCAL_APIC_ADDRESS_OVERRIDE
{
    UINT8 Type;
    UINT8 Length;
    UINT16 Reserved;
    UINT64 LocalApicAddress;
} PACKED LOCAL_APIC_ADDRESS_OVERRIDE;

static STATUS API ParseLocalApic(IN CONST UINT64 LocalApicAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration);
static STATUS API ParseIoApic(IN CONST UINT64 IoApicAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration);
static STATUS API ParseInterruptSourceOverride(IN CONST UINT64 InterruptSourceOverrideAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration);
static STATUS API ParseNmiSource(IN CONST UINT64 NmiSourceAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration);
static STATUS API ParseLocalApicNmi(IN CONST UINT64 LocalApicNmiAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration);
static STATUS API ParseLocalApicAddressOverride(IN CONST UINT64 LocalApicAddressOverrideAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration);

STATUS API MadtFillSystemConfigurationTable(CONST UINT64 MultipleApicDescriptorTable, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration)
{
    MULTIPLE_APIC_DESCRIPTION_TABLE *Madt = (MULTIPLE_APIC_DESCRIPTION_TABLE *)PhysicalToVirtual(MultipleApicDescriptorTable);

    SdthFillSystemConfigurationTable((UINT64)&Madt->Header, SystemConfiguration);
    LOG_DEBUG(u"Local Interrupt Controller Address: 0x%x", Madt->LocalInterruptControllerAddress);
    LOG_DEBUG(u"Flags: 0x%x", Madt->Flags.Value);

    SystemConfiguration->LocalApicBaseAddress = Madt->LocalInterruptControllerAddress;

    UINT64 Size = sizeof(*Madt);
    UINT64 CurrentAddress = PhysicalToVirtual(MultipleApicDescriptorTable) + Size;

    while (Size < Madt->Header.Length)
    {
        INTERRUPT_CONTROLLER_HEADER *Ich = (INTERRUPT_CONTROLLER_HEADER *)CurrentAddress;
        switch (Ich->Type)
        {
        case EntryTypeLocalApic:
            ParseLocalApic(CurrentAddress, SystemConfiguration);
            break;

        case EntryTypeIoApic:
            ParseIoApic(CurrentAddress, SystemConfiguration);
            break;

        case EntryTypeInterruptSourceOverride:
            ParseInterruptSourceOverride(CurrentAddress, SystemConfiguration);
            break;

        case EntryTypeNmiSource:
            ParseNmiSource(CurrentAddress, SystemConfiguration);
            break;

        case EntryTypeLocalApicNmi:
            ParseLocalApicNmi(CurrentAddress, SystemConfiguration);
            break;

        case EntryTypeLocalApicAddressOverride:
            ParseLocalApicAddressOverride(CurrentAddress, SystemConfiguration);
            break;

        default:
            LOG_WARNING(u"Skipped entry: 0x%x", Ich->Type);
            break;
        }

        CurrentAddress += Ich->Length;
        Size += Ich->Length;
    }

    return E_OK;
}

static STATUS API ParseLocalApic(IN CONST UINT64 LocalApicAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration)
{
    LOCAL_APIC *LocalApic = (LOCAL_APIC *)LocalApicAddress;

    LOG_DEBUG(u"Local Apic Processor Id: 0x%x", LocalApic->ProcessorId);
    LOG_DEBUG(u"Local Apic Processor Uid: 0x%x", LocalApic->ProcessorUid);
    LOG_DEBUG(u"Local Apic Processor Flags: 0x%x", LocalApic->Flags);

    (VOID) SystemConfiguration;
    return E_OK;
}

static STATUS API ParseIoApic(IN CONST UINT64 IoApicAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration)
{
    IO_APIC *IoApic = (IO_APIC *)IoApicAddress;

    LOG_DEBUG(u"Io Apic Id: 0x%x", IoApic->IoApicId);
    LOG_DEBUG(u"Io Apic Address: 0x%x", IoApic->IoApicAddress);
    LOG_DEBUG(u"Io Apic Global System Interrupt Base: 0x%x", IoApic->GlobalSystemInterruptBase);

    SystemConfiguration->IoApicBaseAddress = IoApic->IoApicAddress;

    return E_OK;
}

static STATUS API ParseInterruptSourceOverride(IN CONST UINT64 InterruptSourceOverrideAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration)
{
    INTERRUPT_SOURCE_OVERRIDE *InterruptSourceOverride = (INTERRUPT_SOURCE_OVERRIDE *)InterruptSourceOverrideAddress;

    LOG_DEBUG(u"Interrupt Source Override Bus: 0x%x", InterruptSourceOverride->Bus);
    LOG_DEBUG(u"Interrupt Source Override Source: 0x%x", InterruptSourceOverride->Source);
    LOG_DEBUG(u"Interrupt Source Override Global System Interrupt: 0x%x", InterruptSourceOverride->GlobalSystemInterrupt);
    LOG_DEBUG(u"Interrupt Source Override Flags: 0x%x", InterruptSourceOverride->Flags);

    (VOID) SystemConfiguration;
    return E_OK;
}

static STATUS API ParseNmiSource(IN CONST UINT64 NmiSourceAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration)
{
    NMI_SOURCE *NmiSource = (NMI_SOURCE *)NmiSourceAddress;

    LOG_DEBUG(u"NMI Source Flags: 0x%x", NmiSource->Flags);
    LOG_DEBUG(u"NMI Source Global System Interrupt: 0x%x", NmiSource->GlobalSystemInterrupt);

    (VOID) SystemConfiguration;
    return E_OK;
}

static STATUS API ParseLocalApicNmi(IN CONST UINT64 LocalApicNmiAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration)
{
    LOCAL_APIC_NMI *LocalApicNmi = (LOCAL_APIC_NMI *)LocalApicNmiAddress;

    LOG_DEBUG(u"Local Apic NMI Processor UID: 0x%x", LocalApicNmi->ProcessorUid);
    LOG_DEBUG(u"Local Apic NMI Flags: 0x%x", LocalApicNmi->Flags);
    LOG_DEBUG(u"Local Apic NMI Lint Number: 0x%x", LocalApicNmi->LocalApicLintNumber);

    (VOID) SystemConfiguration;
    return E_OK;
}

static STATUS API ParseLocalApicAddressOverride(IN CONST UINT64 LocalApicAddressOverrideAddress, OUT SYSTEM_CONFIGURATION_TABLE *SystemConfiguration)
{
    LOCAL_APIC_ADDRESS_OVERRIDE *LocalApicAddressOverride = (LOCAL_APIC_ADDRESS_OVERRIDE *)LocalApicAddressOverrideAddress;

    LOG_DEBUG(u"Local Apic Address: 0x%x", LocalApicAddressOverride->LocalApicAddress);

    SystemConfiguration->LocalApicBaseAddressOverride = LocalApicAddressOverride->LocalApicAddress;

    return E_OK;
}
