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
        AcquireSRWLockExclusive(&ctx->ContextLock);
        ctx->Number++;
        ReleaseSRWLockExclusive(&ctx->ContextLock);
    }

    return STATUS_SUCCESS;
}
