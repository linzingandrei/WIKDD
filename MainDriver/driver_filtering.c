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
        case RegNtPreSetValueKey:
            object = ((PREG_SET_VALUE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostSetValueKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreDeleteValueKey:
            object = ((PREG_DELETE_VALUE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostDeleteValueKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreDeleteKey:
            object = ((PREG_DELETE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostDeleteKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostLoadKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostUnLoadKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreRenameKey:
            object = ((PREG_RENAME_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostRenameKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostQueryValueKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostCreateKeyEx:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostOpenKeyEx:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreSaveKey:
            object = ((PREG_SAVE_KEY_INFORMATION)pParameters)->Object;
            break;

        case RegNtPostSaveKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;

            break;
        case RegNtPreQueryValueKey:
            object = ((PREG_QUERY_VALUE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreCreateKey:
            // object = ((PREG_PRE_CREATE_KEY_INFORMATION)pParameters)->Object;
            // object is not created yet
            break;
        case RegNtPreCreateKeyEx:
            // object = ((PREG_CREATE_KEY_INFORMATION)pParameters)->Object;
            // object is not created yet
            break;
        case RegNtPostCreateKey:
            object = ((PREG_POST_CREATE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreSetInformationKey:
            object = ((PREG_SET_INFORMATION_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostSetInformationKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreEnumerateKey:
            object = ((PREG_ENUMERATE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostEnumerateKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreEnumerateValueKey:
            object = ((PREG_ENUMERATE_VALUE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostEnumerateValueKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreQueryKey:
            object = ((PREG_QUERY_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostQueryKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreQueryMultipleValueKey:
            object = ((PREG_QUERY_MULTIPLE_VALUE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostQueryMultipleValueKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreOpenKey:
            // object = ((PREG_PRE_OPEN_KEY_INFORMATION)pParameters)->Object;
            // object is not created yet
            break;
        case RegNtPreOpenKeyEx:
            // object = ((PREG_OPEN_KEY_INFORMATION)pParameters)->Object;
            // object is not created yet
            break;
        case RegNtPostOpenKey:
            object = ((PREG_POST_OPEN_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreKeyHandleClose:
            object = ((PREG_KEY_HANDLE_CLOSE_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostKeyHandleClose:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreFlushKey:
            object = ((PREG_FLUSH_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostFlushKey:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreLoadKey:
            object = ((PREG_LOAD_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreUnLoadKey:
            object = ((PREG_UNLOAD_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreQueryKeySecurity:
            object = ((PREG_QUERY_KEY_SECURITY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostQueryKeySecurity:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreSetKeySecurity:
            object = ((PREG_SET_KEY_SECURITY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostSetKeySecurity:
            object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
            break;
        case RegNtCallbackObjectContextCleanup:
            object = ((PREG_CALLBACK_CONTEXT_CLEANUP_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreRestoreKey:
            object = ((PREG_RESTORE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostRestoreKey:
            object = ((PREG_RESTORE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPreReplaceKey:
            object = ((PREG_REPLACE_KEY_INFORMATION)pParameters)->Object;
            break;
        case RegNtPostReplaceKey:
            object = ((PREG_REPLACE_KEY_INFORMATION)pParameters)->Object;
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