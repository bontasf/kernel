#include "uart.h"
#include "port.h"

#define COM1 0x2F8

void UartInit(void)
{
    // Disable all UART interrupts
    PortWriteByte(COM1 + 1, 0x00);

    // Enable DLAB (set baud rate divisor)
    PortWriteByte(COM1 + 3, 0x80);

    // Set divisor to 3 (38400 baud if base clock 115200)
    PortWriteByte(COM1 + 0, 0x03); // Divisor low byte
    PortWriteByte(COM1 + 1, 0x00); // Divisor high byte

    // 8 bits, no parity, 1 stop bit (8N1)
    PortWriteByte(COM1 + 3, 0x03);

    // Enable FIFO, clear them, 14-byte threshold
    PortWriteByte(COM1 + 2, 0xC7);

    // Modem control:
    // Bit 0 = DTR
    // Bit 1 = RTS
    // DO NOT set OUT2 (bit 3)
    PortWriteByte(COM1 + 4, 0x03);

    UINT8 mask;
    PortReadByte(0x21, &mask);
    PortWriteByte(0x21, mask | (1 << 4));
}

static void UartWriteChar(CHAR8 Character)
{
    UINT8 Result = 0;
    do
    {
        PortReadByte(COM1 + 5, &Result);
    }
    while ((Result & 0x20) == 0);
    PortWriteByte(COM1, Character);
}

void UartWriteString(CHAR8 *InputString)
{
    while (*InputString)
    {
        if (*InputString == '\n')
            UartWriteChar('\r');
        UartWriteChar(*InputString++);
    }
}

void UartWriteHex(UINT64 Value)
{
    CONST CHAR8* HexMap = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4)
    {
        UartWriteChar(HexMap[(Value >> i) & 0xF]);
    }
}
