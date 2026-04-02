#pragma once
#include "ntddk.h"
#include "wdm.h"


typedef enum
{
    None,
    ItemCreate,
    ItemExit
}ITEM_TYPE;

typedef struct _ITEM_HEADER
{
    ITEM_TYPE Type;
    USHORT Size;
    LARGE_INTEGER Time;
}ITEM_HEADER, *PITEM_HEADER;

typedef struct _PROCESS_CREATE_INFO
{
    ULONG ProcessId;
    ULONG ParentProcessId;
    USHORT CommandLineLength;
    USHORT CommandLineOffset;
}PROCESS_CREATE_INFO, *PPROCESS_CREATE_INFO;

typedef struct _PROCESS_EXIT_INFO
{
    ITEM_HEADER Header;
    ULONG ProcessId;
}PROCESS_EXIT_INFO, *PPROCESS_EXIT_INFO;

VOID
PsCreateProcessNotifyRoutineEx(
    _Inout_ PEPROCESS Process,
    _In_ HANDLE ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo
);

VOID
PLoadImageNotifyRoutine(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo
);

