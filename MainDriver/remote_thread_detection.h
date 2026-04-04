#pragma once

#include "ntifs.h"
#include "ntddk.h"

#define ARRAY_SIZE 10


VOID
PCreateThreadNotifyRoutine(
    _In_ HANDLE ProcessId,
    _In_ HANDLE ThreadId,
    _In_ BOOLEAN Create
);