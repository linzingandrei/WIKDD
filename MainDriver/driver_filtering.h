#pragma once
#ifdef MY_DRIVER_BUILD
#include "ntifs.h"


LARGE_INTEGER RegCookie;

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

_IRQL_requires_same_
NTSTATUS
OnRegistryNotify(
    _In_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2
);

#endif