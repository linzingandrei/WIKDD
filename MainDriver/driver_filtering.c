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

VOID
PLoadImageNotifyRoutine(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo
)
{
    UNREFERENCED_PARAMETER(ProcessId);
    UNREFERENCED_PARAMETER(FullImageName);

    PIMAGE_INFO_EX imgInfo = CONTAINING_RECORD(ImageInfo, IMAGE_INFO_EX, ImageInfo);

    UNICODE_STRING imageFileName;
    imageFileName.Length = imgInfo->FileObject->FileName.Length;
    imageFileName.MaximumLength = imgInfo->FileObject->FileName.MaximumLength;
    imageFileName.Buffer = ExAllocatePool2(POOL_FLAG_PAGED, imageFileName.MaximumLength, 'imN');

    if (imageFileName.Buffer != NULL)
    {
        //__debugbreak();

        RtlCopyMemory(imageFileName.Buffer, imgInfo->FileObject->FileName.Buffer, imageFileName.Length);

        DbgPrintEx(0, 0, "Image file name: %wZ\n", &imageFileName);

        ExFreePoolWithTag(imageFileName.Buffer, 'prN');
    }
}
