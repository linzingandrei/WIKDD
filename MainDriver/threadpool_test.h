#pragma once

#include "Windows.h"
#include "ntstatus.h"
#include "ntifs.h"

typedef struct _MY_CONTEXT
{
    PKSPIN_LOCK ContextLock;
    KIRQL oldIrql;
    UINT32 Number;
} MY_CONTEXT;

DWORD WINAPI
TestThreadPoolRoutine(
    _In_opt_ PVOID Context
);