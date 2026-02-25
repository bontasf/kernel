#ifndef _SYSTEM_LOG_H_
#define _SYSTEM_LOG_H_

#include "platform_types.h"

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_WARNING 2
#define LOG_LEVEL_ERROR 3

#define LOG_LEVEL LOG_LEVEL_DEBUG
#define CONSOLE_LOGGING

typedef VOID (API *LOG_CONSUMER)(IN CONST CHAR16 *Message, IN CONST UINT32 MessageColor);

VOID LogInit(VOID);
VOID LogRegisterConsumer(CONST LOG_CONSUMER LogConsumer);
VOID LogDebug(IN CONST CHAR16 *ModuleTag, IN CONST CHAR16 *Format, ...);
VOID LogInfo(IN CONST CHAR16 *ModuleTag, IN CONST CHAR16 *Format, ...);
VOID LogWarning(IN CONST CHAR16 *ModuleTag, IN CONST CHAR16 *Format, ...);
VOID LogError(IN CONST CHAR16 *ModuleTag, IN CONST CHAR16 *Format, ...);

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(Format, ...) LogDebug(MODULE_TAG, Format, ##__VA_ARGS__);
#else
#define LOG_DEBUG(Format, ...)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(Format, ...) LogInfo(MODULE_TAG, Format, ##__VA_ARGS__);
#else
#define LOG_INFO(Format, ...)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARNING
#define LOG_WARNING(Format, ...) LogWarning(MODULE_TAG, Format, ##__VA_ARGS__);
#else
#define LOG_WARNING(Format, ...)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(Format, ...) LogError(MODULE_TAG, Format, ##__VA_ARGS__);
#else
#define LOG_ERROR(Format, ...)
#endif

#endif /* _SYSTEM_LOG_H_ */
