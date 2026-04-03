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

        ExFreePoolWithTag(imageFileName.Buffer, 'imN');
    }
}

_IRQL_requires_same_
NTSTATUS
OnRegistryNotify(
    _In_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2
)
{
    UNREFERENCED_PARAMETER(CallbackContext);

    REG_NOTIFY_CLASS regNotifyClass = (REG_NOTIFY_CLASS)(SIZE_T)Argument1;
    PVOID pParameters = Argument2;
    PVOID object = NULL;

    switch (regNotifyClass)
    {
        case RegNtPreRenameKey:
            object = ((PREG_RENAME_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostRenameKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
   
        default:
            break;
    }

    if (regNotifyClass == RegNtQueryValueKey || regNotifyClass == RegNtPreQueryValueKey || regNotifyClass == RegNtPostQueryValueKey)
    {
        return STATUS_SUCCESS;
    }

    if (object)
    {
        ULONG_PTR objectId;
        PUNICODE_STRING objectName;
        NTSTATUS status = STATUS_UNSUCCESSFUL;
        
        status = CmCallbackGetKeyObjectIDEx(&RegCookie, object, &objectId, &objectName, 0);
        if (!NT_SUCCESS(status))
        {
            DbgPrintEx(0, 0, "CmCallbackGetKeyObjectIDEx failed with status = 0x%X\n", status);
        }
        else
        {
            DbgPrintEx(0, 0, "Key: %wZ\n", objectName);
            CmCallbackReleaseKeyObjectIDEx(objectName);
        }
    }

    return STATUS_SUCCESS;
}