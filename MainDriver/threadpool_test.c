#include "threadpool_test.h"


NTSTATUS
TestThreadPoolRoutine(
    _In_opt_ PVOID Context
)
{
    MY_CONTEXT* ctx = (MY_CONTEXT*)(Context);
    if (NULL == ctx)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (UINT32 i = 0; i < 1000; ++i)
    {
        KIRQL oldIrql;
        KeAcquireSpinLock(&ctx->ContextLock, &oldIrql);
        ctx->Number++;
        KeReleaseSpinLock(&ctx->ContextLock, oldIrql);
    }

    return STATUS_SUCCESS;
}
