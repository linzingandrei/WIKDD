#include <ntifs.h>

#include "threadpool.h"

_IRQL_requires_same_
_Function_class_(KSTART_ROUTINE)
VOID
TpWorkerThread(
    _In_ PVOID StartContext
)
{
    //__debugbreak();

    PMY_THREAD_POOL threadPool = (PMY_THREAD_POOL)(StartContext);
    PVOID objects[2] = { &threadPool->StopThreadPool, &threadPool->WorkScheduled };

    BOOLEAN executing = TRUE;
    while (executing)
    {
        BOOLEAN shouldWork = FALSE;
        const NTSTATUS status = KeWaitForMultipleObjects(ARRAYSIZE(objects), objects, WaitAny, Executive, KernelMode, FALSE, NULL, NULL);

        switch (status)
        {
        case STATUS_WAIT_0 + 1:
            shouldWork = TRUE;
            break;
        case STATUS_WAIT_0:
        default:
            executing = FALSE;
            continue;
            break;
        }

        while (shouldWork)
        {
            KIRQL oldIrql;
            KeAcquireSpinLock(&threadPool->PoolLock, &oldIrql);

            if (!IsListEmpty(&threadPool->ListHead))
            {
                LIST_ENTRY* entry = RemoveTailList(&threadPool->ListHead);
                MY_WORK_ITEM* workItem = CONTAINING_RECORD(entry, MY_WORK_ITEM, ListEntry);

                workItem->Routine(workItem->Context);
                ExFreePoolWithTag(workItem, 'KMSD');
            }
            else
            {
                shouldWork = FALSE;
            }

            KeReleaseSpinLock(&threadPool->PoolLock, oldIrql);
        }
    }
}

VOID
TpUninit(
    _Pre_valid_ _Post_invalid_ PMY_THREAD_POOL ThreadPool
)
{
    KeSetEvent(&ThreadPool->StopThreadPool, 0, FALSE);

    for (UINT32 i = 0; i < ThreadPool->NumberOfThreads; i++)
    {
        if (NULL != ThreadPool->ThreadHandles[i])
        {
            ZwWaitForSingleObject(ThreadPool->ThreadHandles[i], FALSE, NULL);
            ZwClose(ThreadPool->ThreadHandles[i]);
            ThreadPool->ThreadHandles[i] = NULL;
        }
    }
    if (ThreadPool->ThreadHandles)
    {
        ExFreePoolWithTag(ThreadPool->ThreadHandles, 'KMSD');
        ThreadPool->ThreadHandles = NULL;
    }

    while (!IsListEmpty(&ThreadPool->ListHead))
    {
        KIRQL oldIrql;
        KeAcquireSpinLock(&ThreadPool->PoolLock, &oldIrql);

        LIST_ENTRY* entry = RemoveHeadList(&ThreadPool->ListHead);
        MY_WORK_ITEM* workItem = CONTAINING_RECORD(entry, MY_WORK_ITEM, ListEntry);

        workItem->Routine(workItem->Context);
        ExFreePoolWithTag(workItem, 'KMSD');

        KeReleaseSpinLock(&ThreadPool->PoolLock, oldIrql);
    }
}

NTSTATUS
TpInit(
    _In_ PMY_THREAD_POOL ThreadPool,
    _In_ UINT32          NumberOfThreads
)
{
    if (!NumberOfThreads)
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    InitializeListHead(&ThreadPool->ListHead);
    KeInitializeMutex(&ThreadPool->ListMutex, 0);

    KeInitializeEvent(&ThreadPool->StopThreadPool, NotificationEvent, FALSE);

    KeInitializeEvent(&ThreadPool->WorkScheduled, SynchronizationEvent, FALSE);

    ThreadPool->NumberOfThreads = NumberOfThreads;
    ThreadPool->ThreadHandles = ExAllocatePool2(POOL_FLAG_NON_PAGED, NumberOfThreads * sizeof(HANDLE), 'KMSD');

    if (!ThreadPool->ThreadHandles)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(ThreadPool->ThreadHandles, NumberOfThreads * sizeof(HANDLE));

    for (UINT32 i = 0; i < NumberOfThreads; i++)
    {
        HANDLE hThread = NULL;
        const NTSTATUS status = PsCreateSystemThread(&hThread, 0, NULL, NULL, NULL, TpWorkerThread, ThreadPool);

        if (!NT_SUCCESS(status))
        {
            TpUninit(ThreadPool);
            return status;
        }
        else
        {
            ThreadPool->ThreadHandles[i] = hThread;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
TpEnqueueWorkItem(
    _In_ PMY_THREAD_POOL ThreadPool,
    _In_ PKSTART_ROUTINE StartRoutine,
    _Inout_opt_ PVOID    Context
)
{
    MY_WORK_ITEM* workItem = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MY_WORK_ITEM), 'KMSD');

    if (!workItem)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    workItem->Routine = StartRoutine;
    workItem->Context = Context;

    KIRQL oldIrql;
    KeAcquireSpinLock(&ThreadPool->PoolLock, &oldIrql);
    InsertHeadList(&ThreadPool->ListHead, &workItem->ListEntry);
    KeReleaseSpinLock(&ThreadPool->PoolLock, oldIrql);

    KeSetEvent(&ThreadPool->WorkScheduled, 0, FALSE);

    return STATUS_SUCCESS;
}
