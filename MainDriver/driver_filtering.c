#include "driver_filtering.h"


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

        if (commandLineSize > 0)
        {
            CHAR CommandLine[1000] = { 0 };
            memcpy(CommandLine, CreateInfo->CommandLine->Buffer, commandLineSize);
            CommandLine[commandLineSize] = '\0';
            
            DbgPrintEx(0, 0, "Command line size: %d\n", commandLineSize);

            DbgPrintEx(0, 0, "Process command line: ");
            for (int i = 0; i < commandLineSize; i++)
            {
                DbgPrintEx(0, 0, "%c", CommandLine[i]);
            }

            DbgPrintEx(0, 0, "\n");
        }
    }
}