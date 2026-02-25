#include "system_log.h"
#include "console_controller.h"
#include "system_physical_memory.h"

#define OUTPUT_TYPE_MARKER (u'%')
#define INT64_TYPE (u'd')
#define UINT64_TYPE (u'u')
#define HEX_TYPE (u'x')
#define STRING_TYPE (u's')

#define DEFAULT_TEXT_COLOR 0x00FFFFFF
#define DEBUGG_TEXT_COLOR 0x00FFFFFF
#define INFO_TEXT_COLOR 0x0000FF00
#define WARNING_TEXT_COLOR 0x0000FFFF
#define ERROR_TEXT_COLOR 0x00FF0000

#define MAXIMUM_LOG_CONSUMERS 0x10U

static LOG_CONSUMER LogConsumers[MAXIMUM_LOG_CONSUMERS];
static UINT8 NumberOfConsumers = 0;

static CHAR16 *AddInt(IN OUT CHAR16 *Output, IN INT64 Value);
static CHAR16 *AddUint(IN OUT CHAR16 *Output, IN UINT64 Value);
static CHAR16 *AddHex(IN OUT CHAR16 *Output, IN UINT64 Value);
static CHAR16 *AddString(IN OUT CHAR16 *Output, IN CONST CHAR16 *String);
static VOID BuildOutputString(OUT CHAR16 *Output, IN CONST CHAR16 *Format, ...);
static VOID BuildFormatOutputString(OUT CHAR16 *Output, IN CONST CHAR16 *Format, __builtin_va_list ArgumentList);

VOID LogInit(VOID)
{
    NumberOfConsumers = 0;
    for (UINT64 Index = 0; Index < MAXIMUM_LOG_CONSUMERS; Index ++)
    {
        LogConsumers[Index] = NULL_PTR;
    }
}

VOID LogDebug(IN CONST CHAR16 *ModuleTag, IN CONST CHAR16 *Format, ...)
{
    CHAR16 FormatOutput[1024];
    CHAR16 Output[1024];
    __builtin_va_list ArgumentList;
    __builtin_va_start(ArgumentList, Format);
    BuildFormatOutputString(FormatOutput, Format, ArgumentList);
    BuildOutputString(Output, u"%s: %s\n", ModuleTag, FormatOutput);

    for (UINT64 Index = 0; Index < NumberOfConsumers; Index ++)
    {
        LogConsumers[Index](Output, DEBUGG_TEXT_COLOR);
    }
}
VOID LogInfo(IN CONST CHAR16 *ModuleTag, IN CONST CHAR16 *Format, ...)
{
    CHAR16 FormatOutput[1024];
    CHAR16 Output[1024];
    __builtin_va_list ArgumentList;
    __builtin_va_start(ArgumentList, Format);
    BuildFormatOutputString(FormatOutput, Format, ArgumentList);
    BuildOutputString(Output, u"%s: %s\n", ModuleTag, FormatOutput);

    for (UINT64 Index = 0; Index < NumberOfConsumers; Index ++)
    {
        LogConsumers[Index](Output, DEBUGG_TEXT_COLOR);
    }
}
VOID LogWarning(IN CONST CHAR16 *ModuleTag, IN CONST CHAR16 *Format, ...)
{
    CHAR16 FormatOutput[1024];
    CHAR16 Output[1024];
    __builtin_va_list ArgumentList;
    __builtin_va_start(ArgumentList, Format);
    BuildFormatOutputString(FormatOutput, Format, ArgumentList);
    BuildOutputString(Output, u"%s: %s\n", ModuleTag, FormatOutput);

    for (UINT64 Index = 0; Index < NumberOfConsumers; Index ++)
    {
        LogConsumers[Index](Output, DEBUGG_TEXT_COLOR);
    }
}
VOID LogError(IN CONST CHAR16 *ModuleTag, IN CONST CHAR16 *Format, ...)
{
    CHAR16 FormatOutput[1024];
    CHAR16 Output[1024];
    __builtin_va_list ArgumentList;
    __builtin_va_start(ArgumentList, Format);
    BuildFormatOutputString(FormatOutput, Format, ArgumentList);
    BuildOutputString(Output, u"%s: %s\n", ModuleTag, FormatOutput);

    for (UINT64 Index = 0; Index < NumberOfConsumers; Index ++)
    {
        LogConsumers[Index](Output, DEBUGG_TEXT_COLOR);
    }
}

VOID LogRegisterConsumer(CONST LOG_CONSUMER LogConsumer)
{
    LogConsumers[NumberOfConsumers] = LogConsumer;
    NumberOfConsumers ++;
}

static CHAR16 *AddInt(IN OUT CHAR16 *Output, IN INT64 Value)
{
    UINT64 Lenght = 0;

    if (Value < 0)
    {
        Output[0] = u'-';
        Output++;
    }

    do
    {
        Output[Lenght++] = u'0' + (Value % 10);
        Value /= 10;
    } while (Value > 0);

    for (UINT64 i = 0; i < Lenght / 2; ++i)
    {
        CHAR16 Charater = Output[i];
        Output[i] = Output[Lenght - i - 1];
        Output[Lenght - i - 1] = Charater;
    }

    return Output + Lenght;
}

static CHAR16 *AddUint(IN OUT CHAR16 *Output, IN UINT64 Value)
{
    UINT64 Lenght = 0;

    do
    {
        Output[Lenght++] = u'0' + (Value % 10);
        Value /= 10;
    } while (Value > 0);

    for (UINT64 i = 0; i < Lenght / 2; ++i)
    {
        CHAR16 Charater = Output[i];
        Output[i] = Output[Lenght - i - 1];
        Output[Lenght - i - 1] = Charater;
    }

    return Output + Lenght;
}

static CHAR16 *AddHex(IN OUT CHAR16 *Output, IN UINT64 Value)
{
    UINT64 Lenght = 0;
    CHAR16 Charater = 0;

    do
    {
        if (Value % 16 < 10)
        {
            Charater = u'0' + (Value % 16);
        }
        else
        {
            Charater = u'A' - 10 + (Value % 16);
        }
        Output[Lenght++] = Charater;
        Value /= 16;
    } while (Value > 0);

    for (UINT64 i = 0; i < Lenght / 2; ++i)
    {
        Charater = Output[i];
        Output[i] = Output[Lenght - i - 1];
        Output[Lenght - i - 1] = Charater;
    }

    return Output + Lenght;
}
static CHAR16 *AddString(IN OUT CHAR16 *Output, IN CONST CHAR16 *String)
{
    for (UINT64 i = 0; u'\0' != String[i]; ++i)
    {
        Output[0] = String[i];
        Output++;
    }

    return Output;
}

static VOID BuildOutputString(OUT CHAR16 *Output, IN CONST CHAR16 *Format, ...)
{
    __builtin_va_list ArgumentList;
    __builtin_va_start(ArgumentList, Format);
    BuildFormatOutputString(Output, Format, ArgumentList);
}

static VOID BuildFormatOutputString(OUT CHAR16 *Output, IN CONST CHAR16 *Format, __builtin_va_list ArgumentList)
{
    BOOLEAN OutputTypeMarker = FALSE;
    INT64 Int64Value = 0;
    UINT64 Uint64Value = 0;
    CONST CHAR16 *StringValue = 0;

    for (UINT64 i = 0; u'\0' != Format[i]; ++i)
    {
        if (FALSE == OutputTypeMarker)
        {
            if (OUTPUT_TYPE_MARKER == Format[i])
            {
                OutputTypeMarker = TRUE;
            }
            else
            {
                Output[0] = Format[i];
                Output++;
            }
        }
        else
        {
            switch (Format[i])
            {
            case INT64_TYPE:
                Int64Value = __builtin_va_arg(ArgumentList, INT64);
                Output = AddInt(Output, Int64Value);
                break;

            case UINT64_TYPE:
                Uint64Value = __builtin_va_arg(ArgumentList, UINT64);
                Output = AddUint(Output, Uint64Value);
                break;

            case HEX_TYPE:
                Uint64Value = __builtin_va_arg(ArgumentList, UINT64);
                Output = AddHex(Output, Uint64Value);
                break;

            case STRING_TYPE:
                StringValue = __builtin_va_arg(ArgumentList, CONST CHAR16 *);
                Output = AddString(Output, StringValue);
                break;
            default:
                Output[0] = u'%';
                Output++;
                break;
            }
            OutputTypeMarker = FALSE;
        }
    }

    Output[0] = '\0';
    __builtin_va_end(ArgumentList);
}
