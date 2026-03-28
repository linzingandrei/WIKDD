#include "threadpool_test.h"


DWORD WINAPI
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
        KeAcquireSpinLock(&ctx->ContextLock, &ctx->oldIrql);
        ctx->Number++;
        KeReleaseSpinLock(&ctx->ContextLock, &ctx->oldIrql);
    }

    return STATUS_SUCCESS;
}
