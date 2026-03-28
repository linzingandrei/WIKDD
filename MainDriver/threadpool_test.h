#pragma once

#include "Windows.h"
#include "ntstatus.h"

typedef struct _MY_CONTEXT
{
    SRWLOCK ContextLock;
    UINT32 Number;
} MY_CONTEXT;

DWORD WINAPI
TestThreadPoolRoutine(
    _In_opt_ PVOID Context
);