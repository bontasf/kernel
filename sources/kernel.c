#include "kernel_arguments.h"
#include "system_video.h"
#include "system_physical_memory.h"
#include "utils.h"
#include "console_controller.h"
#include "system_fonts.h"
#include "system_log.h"

#include "global_descriptor_table.h"
#include "interrupt_descriptor_table.h"
#include "local_apic.h"
#include "io_apic.h"
#include "acpi2_parser.h"
#include "system_configuration_table.h"
#include "system_calls.h"
#include "ps2_controller.h"
#include "keyboard_controller.h"
#include "system_keyboard_scan_codes.h"
#include "pic.h"
#include "port.h"
#include "pcie_controller.h"
#include "ahci_controller.h"
#include "system_mass_storage.h"
#include "uart.h"

#define MODULE_TAG u"KERNEL"

extern const CHAR16 KEY_LAYOUY_MAP[NUMBER_OF_SYSTEM_KEY_CODES];

static SYSTEM_CONFIGURATION_TABLE SystemConfigurationTable;
static UINT64 NumberOfPCIeDevices;
static PCIe_HEADER *PCIeDevices[32];


static UINT64 API IdentityMap(UINT64 PhysicalAddress)
{
    return PhysicalAddress;
}

KERNEL_BOOT_INFORMATION KernelBootInformation;

STATUS API KernelEntry(KERNEL_ARGUMENTS *KernelArguments)
{
    UartInit();
    UartWriteString("\nWelcome To Bonta Operating Systems\n");
    KernelBootInformation.PhysicalToVirtualMap = IdentityMap;
    SystemPhysicalMemoryInit(KernelArguments->SystemMemory);
    SystemVideoInit(KernelArguments->VideoAdapter);
    SystemFontsInit(KernelArguments->CharatersBitmap);
    LogInit();

    UINT8 *Buffer = (UINT8 *)&SystemConfigurationTable;

    for (UINT64 i = 0; i < sizeof(SystemConfigurationTable); ++i)
    {
        Buffer[i] = 0;
    }

    Acpi2FillSystemConfigurationTable(KernelArguments->RootSystemDescriptorPointer, &SystemConfigurationTable);
    SystemMassStorageInit();

    PcieControllerInit(SystemConfigurationTable.MemoryMappedConfigurationBaseAddress);
    PcieControllerGetDeviceList(PCIeDevices, &NumberOfPCIeDevices);

    LOG_INFO(u"Found %d PCIe Devices", NumberOfPCIeDevices);
    for (UINT64 i = 0; i < NumberOfPCIeDevices; ++i)
    {
        LOG_INFO(u"Class: 0x%x, Subclass: 0x%x, Programming Interface: 0x%x",
                 PCIeDevices[i]->ClassCode, PCIeDevices[i]->SubclassCode, PCIeDevices[i]->ProgrammingInterface);

        if (PCIeDevices[i]->ClassCode == 1 && PCIeDevices[i]->SubclassCode == 6)
        {
            SystemMassStorageRegisterAhciController(PCIeDevices[i]);

        }
    }

    GDT_ENTRY Entries[5];

    /* Null GDT entry */
    Entries[0].Limit = 0;
    Entries[0].Base = 0;
    Entries[0].AccessByte = 0;
    Entries[0].Flags = 0;

    /* Kernel Mode Code Segment */
    Entries[1].Limit = 0xFFFFF;
    Entries[1].Base = 0;
    Entries[1].AccessByte = 0x9A;
    Entries[1].Flags = 0x0A;

    /* Kernel Mode Data Segment */
    Entries[2].Limit = 0xFFFFF;
    Entries[2].Base = 0;
    Entries[2].AccessByte = 0x92;
    Entries[2].Flags = 0x0C;

    /* User Mode Code Segment */
    Entries[3].Limit = 0xFFFFF;
    Entries[3].Base = 0;
    Entries[3].AccessByte = 0xFA;
    Entries[3].Flags = 0x0A;

    /* User Mode Data Segment */
    Entries[4].Limit = 0xFFFFF;
    Entries[4].Base = 0;
    Entries[4].AccessByte = 0xF2;
    Entries[4].Flags = 0x0C;

    ASM("cli");
    GlobalDescriptorTableInit(Entries, 5);
    PicInit();
    PicDisable();
    InterruptDescriptorTableInit();
    InterruptDescriptorTableSetISR(SYSTEM_CALL_INTERRUPT_VECTOR, (UINT64)SystemCallDispatcher, TYPE_ATTRIBUTES_SYSCALL);
    InterruptDescriptorTableSetISR(KEYBOARD_CONTROLLER_INTERRUPT_VECTOR, (UINT64)KeyboardControllerInterruptHandler, TYPE_ATTRIBUTES_INTERRUPT);

    LocalApicInitialize(SystemConfigurationTable.LocalApicBaseAddress);
    IoApicInit(SystemConfigurationTable.IoApicBaseAddress);
    IoApicRedirectInterrupt(KEYBOARD_CONTROLLER_INTERRUPT_SOURCE_VECTOR, KEYBOARD_CONTROLLER_INTERRUPT_VECTOR);
    PS2ControllerInit();
    KeyboardControllerInit();

    ASM("sti");

    for (UINT8 Index = 0; Index < 255 ; Index++)
    {
            UINT8 Unused;
        PortReadByte(0x64, &Unused);
        while (Unused & 1)
        {
            PortReadByte(0x60, &Unused);
            PortReadByte(0x64, &Unused);
        }
    }

    while (1)
    {
        SYSTEM_KEYBOARD_SCAN_CODE KeyCode = 0;
        CHAR16 KeyCharCode[2];
        KeyCharCode[1] = 0;
        KeyboardControllerGetNextKey(&KeyCode);
        KeyCharCode[0] = KEY_LAYOUY_MAP[KeyCode];

        if (0 != KeyCharCode[0])
        {
            LOG_INFO(u"Pressed: %s", KeyCharCode);
        }
    }

    LOG_INFO(u"%s", u"Pressed");

    while (1)
        ;
    return 0;
}
