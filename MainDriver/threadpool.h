#pragma once

#include <ntifs.h>


typedef struct _MY_THREAD_POOL
{
    KEVENT     StopThreadPool;
    KEVENT     WorkScheduled;
    UINT32     NumberOfThreads;
    HANDLE* ThreadHandles;
    KMUTEX     ListMutex;
    LIST_ENTRY ListHead;
}MY_THREAD_POOL, * PMY_THREAD_POOL;


typedef struct _MY_WORK_ITEM
{
    LIST_ENTRY      ListEntry;
    PKSTART_ROUTINE Routine;
    PVOID           Context;
}MY_WORK_ITEM, * PMY_WORK_ITEM;

_IRQL_requires_same_
_Function_class_(KSTART_ROUTINE)
VOID
TpWorkerThread(
    _In_ PVOID StartContext
);

VOID
TpUninit(
    _Pre_valid_ _Post_invalid_ PMY_THREAD_POOL ThreadPool
);

NTSTATUS
TpInit(
    _In_ PMY_THREAD_POOL ThreadPool,
    _In_ UINT32          NumberOfThreads
);

NTSTATUS
TpEnqueueWorkItem(
    _In_ PMY_THREAD_POOL ThreadPool,
    _In_ PKSTART_ROUTINE StartRoutine,
    _Inout_opt_ PVOID    Context
);