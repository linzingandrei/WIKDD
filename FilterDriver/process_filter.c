#include "process_filter.h"


VOID
PsCreateProcessNotifyRoutineEx(
    _Inout_ PEPROCESS Process,
    _In_ HANDLE ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo
)
{
    UNREFERENCED_PARAMETER(Process);


    if (CreateInfo)
    {
        //USHORT allocSize = sizeof(PROCESS_CREATE_INFO);
        USHORT commandLineSize = 0;

        if (CreateInfo->CommandLine) {
            commandLineSize = CreateInfo->CommandLine->Length;
        }

        //__debugbreak();

        DbgPrintEx(0, 0, "Process id: %d\n", HandleToULong(ProcessId));
        DbgPrintEx(0, 0, "Parent process id: %d\n", HandleToULong(CreateInfo->ParentProcessId));

        UNICODE_STRING processName;
        processName.Length = CreateInfo->FileObject->FileName.Length;
        processName.MaximumLength = CreateInfo->FileObject->FileName.MaximumLength;
        processName.Buffer = ExAllocatePool2(POOL_FLAG_PAGED, processName.MaximumLength, 'prN');

        if (processName.Buffer != NULL)
        {
            //__debugbreak();

            RtlCopyMemory(processName.Buffer, CreateInfo->FileObject->FileName.Buffer, processName.Length);

            DbgPrintEx(0, 0, "Process name: %wZ\n", &processName);

            ExFreePoolWithTag(processName.Buffer, 'prN');
        }

        if (commandLineSize > 0)
        {
            UNICODE_STRING processCommandLine;
            processCommandLine.Length = CreateInfo->CommandLine->Length;
            processCommandLine.MaximumLength = CreateInfo->CommandLine->MaximumLength;
            processCommandLine.Buffer = ExAllocatePool2(POOL_FLAG_PAGED, processCommandLine.MaximumLength, 'prN');

            if (processCommandLine.Buffer != NULL)
            {
                //__debugbreak();

                RtlCopyMemory(processCommandLine.Buffer, CreateInfo->CommandLine->Buffer, processCommandLine.Length);

                DbgPrintEx(0, 0, "Process command line: %wZ\n", &processCommandLine);

                ExFreePoolWithTag(processCommandLine.Buffer, 'prN');
            }
        }
    }
}

NTSTATUS
ProcessFilterInitialize()
{
    return PsSetCreateProcessNotifyRoutineEx(PsCreateProcessNotifyRoutineEx, TRUE);
}

NTSTATUS
ProcessFilterUninitialize()
{
    return PsSetCreateProcessNotifyRoutineEx(PsCreateProcessNotifyRoutineEx, FALSE);
}