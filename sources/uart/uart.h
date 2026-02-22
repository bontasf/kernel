#ifndef _UART_H_
#define _UART_H_

#include "platform_types.h"

void UartInit(void);

void UartWriteString(CHAR8 *InputString);

void UartWriteHex(UINT64 Value);

#endif /* _UART_H_ */