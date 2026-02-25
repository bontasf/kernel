#ifndef _UART_H_
#define _UART_H_

#include "platform_types.h"

void UartInit(void);

void API NO_CALLER_SAVED_REGISTERS UartWriteString(CONST CHAR16 *InputString);

void API NO_CALLER_SAVED_REGISTERS UartWriteHex(UINT64 Value);

#endif /* _UART_H_ */