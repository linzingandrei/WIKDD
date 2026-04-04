#include "remote_thread_detection.h"


VOID
PCreateThreadNotifyRoutine(
    _In_ HANDLE ProcessId,
    _In_ HANDLE ThreadId,
    _In_ BOOLEAN Create
)
{
    if (Create)
    {
        //__debugbreak();

        DbgPrintEx(0, 0, "Process with PID %d created thread with TID %d\n", ProcessId, ThreadId);

        PVOID stackTrace[ARRAY_SIZE] = { 0 };
        USHORT capturedFrames = 0;

        capturedFrames = RtlCaptureStackBackTrace(0, ARRAY_SIZE, stackTrace, NULL);

        for (USHORT i = 0; i < capturedFrames; i++)
        {
            DbgPrintEx(0, 0, "Frame %d with address 0x%X\n", i, stackTrace[i]);
        }

        DbgPrintEx(0, 0, "===========================================================================\n");
    }
}