.POSIX:
.PHONY: build clean

INCLUDE_DIRS = -Isources/kernel_arguments \
			   -Isources/system_physical_memory \
			   -Isources/system_video \
			   -Isources/application_view \
			   -Isources/system_fonts \
			   -Isources/console_controller \
			   -Isources/system_log \
			   -Isources/global_descriptor_table \
			   -Isources/interrupt_descriptor_table \
			   -Isources/port \
			   -Isources/io_apic \
			   -Isources/acpi2_parser \
			   -Isources/pic \
			   -Isources/uart \
			   -Isources/local_apic \
			   -Isources/system_calls \
			   -Isources/keyboard_controller \
			   -Isources/keyboard_scan_code \
			   -Isources/ps2_controller \
			   -Isources/pcie_controller \
			   -Isources/mass_storage/ahci_controller \
			   -Isources/mass_storage/partition_manager \
			   -Isources/system_mass_storage \
			   -Isources/msr \
               -I../bootloader/sources \
			   -I../platform \
			   -I../utils

SOURCES = sources/kernel.c \
          sources/system_physical_memory/system_physical_memory.c \
          sources/system_video/system_video.c \
		  sources/application_view/application_view.c \
		  sources/system_fonts/system_fonts.c \
		  sources/console_controller/console_controller.c \
		  sources/system_log/system_log.c \
		  sources/global_descriptor_table/global_descriptor_table.c \
		  sources/interrupt_descriptor_table/interrupt_descriptor_table.c \
		  sources/system_calls/system_calls.c \
		  sources/keyboard_controller/keyboard_controller.c \
		  sources/keyboard_scan_code/keyboard_scan_code_set_1.c \
		  sources/keyboard_scan_code/keyboard_layout.c \
		  sources/ps2_controller/ps2_controller.c \
		  sources/pcie_controller/pcie_controller.c \
		  sources/mass_storage/ahci_controller/ahci_controller.c \
		  sources/mass_storage/ahci_controller/port_controller/sata_port_controller/sata_port_controller.c \
		  sources/mass_storage/partition_manager/partition_manager.c \
		  sources/mass_storage/partition_manager/file_systems/fat32/fat32_partition_manager.c \
		  sources/system_mass_storage/system_mass_storage.c \
		  sources/local_apic/local_apic.c \
		  sources/io_apic/io_apic.c \
		  sources/uart/uart.c \
		  sources/acpi2_parser/acpi2_parser.c \
		  sources/acpi2_parser/xsdt_parser/xsdt_parser.c \
		  sources/acpi2_parser/sdth_parser/sdth_parser.c \
		  sources/acpi2_parser/madt_parser/madt_parser.c \
		  sources/acpi2_parser/mcfg_parser/mcfg_parser.c \
		  sources/pic/pic.c \
		  sources/port/port.c \
		  sources/msr/msr.c \
		  ../utils/utils.c

OBJS = $(SOURCES:.c=.o)
DEPENDENCIES = $(SOURCES:.c=.d)

BUILD_TARGET = Kernel.bin

CC = clang

LD = ld

LDFLAGS = \
	-fPIE \
	-e KernelEntry \
	-nostdlib


CFLAGS = \
	-std=c17 \
	-fPIE \
	-MMD \
	-Wall \
	-Wextra \
	-Wpedantic \
	-mno-red-zone \
	-ffreestanding

build: $(BUILD_TARGET)

$(BUILD_TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	rm -rf build
	mkdir build
	mv $(BUILD_TARGET) build/$(BUILD_TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

-include $(DEPENDS)

clean:
	rm -rf build $(BUILD_TARGET) $(OBJS) $(DEPENDENCIES)
