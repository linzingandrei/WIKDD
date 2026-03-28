#pragma once

#include "ntstatus.h"
#include "ntifs.h"

typedef struct _MY_CONTEXT
{
    KSPIN_LOCK ContextLock;
    UINT32 Number;
} MY_CONTEXT;

NTSTATUS
TestThreadPoolRoutine(
    _In_opt_ PVOID Context
);

NTSTATUS
SimpleTPProcess(
    _In_opt_ PVOID Context
);