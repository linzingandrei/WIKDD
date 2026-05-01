#include <fltKernel.h>
#include <wdm.h>
#include "ntstrsafe.h"
#include "threadpool.h"

#include "trace.h"
#include "main.tmh"


#define UTILS_TAG_UNICODE_STRING 'hcu$'

UNICODE_STRING gAltitude;
LARGE_INTEGER gRegistryCookie = { 0 };

PFLT_FILTER gFilterRegistration = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;
PDRIVER_OBJECT gDriverObject = NULL;

KSPIN_LOCK gClientPortLock;

BOOLEAN gProcessMonitoringEnabled = FALSE;
BOOLEAN gImageMonitoringEnabled = FALSE;
BOOLEAN gThreadMonitoringEnabled = FALSE;
BOOLEAN gRegistryMonitoringEnabled = FALSE;
BOOLEAN gFileMonitoringEnabled = FALSE;

DRIVER_INITIALIZE DriverEntry;

typedef struct _REPLY_DATA
{
    WCHAR message[1024];
    ULONG messageLength;
} REPLY_DATA, *PREPLY_DATA;

typedef struct _MY_CUSTOM_MESSAGE
{
    FILTER_MESSAGE_HEADER headers;
    REPLY_DATA replyData;
    
} MY_CUSTOM_MESSAGE, * PMY_CUSTOM_MESSAGE;

BOOLEAN hasBeenInitialized = FALSE;

typedef struct _MY_CONTEXT
{
    KSPIN_LOCK ContextLock;
    UINT32 Number;
} MY_CONTEXT;

typedef struct _MY_THREADPOOL {
    MY_THREAD_POOL tp;
    MY_CONTEXT ctx;

    int numberOfThreadPools;
} MY_THREADPOOL, *PMY_THREADPOOL;

PMY_THREADPOOL gThreadPool = NULL;

typedef
NTSTATUS
(NTAPI* PFUNC_ZwQueryInformationProcess) (
    _In_      HANDLE           ProcessHandle,
    _In_      PROCESSINFOCLASS ProcessInformationClass,
    _Out_     PVOID            ProcessInformation,
    _In_      ULONG            ProcessInformationLength,
    _Out_opt_ PULONG           ReturnLength
);

PFUNC_ZwQueryInformationProcess pfnZwQueryInformationProcess;

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
* Prototypes
*/

NTSTATUS
ConnectNotifyCallback(
    PFLT_PORT ClientPort,
    PVOID ServerPortCookie,
    PVOID ConnectionContext,
    ULONG SizeOfContext,
    PVOID* ConnectionPortCookie
);

VOID
DisconnectNotifyCallback(
    PVOID ConnectionCookie
);

NTSTATUS
MessageNotifyCallback(
    PVOID PortCookie,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength,
    PULONG ReturnOutputBufferLength
);

NTSTATUS CreateCommunicationPort();

FLT_PREOP_CALLBACK_STATUS PreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Out_ PVOID* Buffer
);

NTSTATUS
FilterUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
);

VOID SendWorker(
    PVOID ctx
);

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
* Utils
*/

NTSTATUS
GetImagePathFromOpenHandle(
    _In_  HANDLE hProcess,
    _Out_ PUNICODE_STRING* ProcessPath
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    ULONG dwObjectNameSize = 0;
    PUNICODE_STRING pProcessPath = NULL;

    __try
    {
        //__debugbreak();
        // get the size of the process name
        status = pfnZwQueryInformationProcess(hProcess,
            ProcessImageFileName, NULL,
            dwObjectNameSize, &dwObjectNameSize);
        if (STATUS_INFO_LENGTH_MISMATCH != status)
        {
            __leave;
        }

        // allocate required space
        pProcessPath = (PUNICODE_STRING)ExAllocatePool2(POOL_FLAG_NON_PAGED,
            dwObjectNameSize, UTILS_TAG_UNICODE_STRING);
        if (!pProcessPath)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            __leave;
        }

        // get the name
        status = pfnZwQueryInformationProcess(hProcess, ProcessImageFileName,
            pProcessPath, dwObjectNameSize, &dwObjectNameSize);
        if (!NT_SUCCESS(status))
        {
            __leave;
        }
        *ProcessPath = pProcessPath;
        status = STATUS_SUCCESS;
    }
    __finally
    {
        if (!NT_SUCCESS(status))
        {
            if (pProcessPath)
            {
                ExFreePoolWithTag(pProcessPath, UTILS_TAG_UNICODE_STRING);
            }
        }
    }
    return status;
}

NTSTATUS
GetImagePathFromPid(
    _In_  HANDLE Pid,
    _Out_ PUNICODE_STRING* ProcessPath
)
{
    HANDLE hProcess;
    OBJECT_ATTRIBUTES objattr;
    CLIENT_ID clientId;

    clientId.UniqueProcess = Pid;
    clientId.UniqueThread = NULL;

    InitializeObjectAttributes(&objattr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS status = ZwOpenProcess(&hProcess, GENERIC_ALL, &objattr, &clientId);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("ZwOpenProcess failed. Status = 0x%x", status);
        return status;
    }

    status = GetImagePathFromOpenHandle(hProcess, ProcessPath);
    ZwClose(hProcess);
    return status;
}

NTSTATUS GetCurrentProcessImagePath(_Out_ PUNICODE_STRING* ProcessPath)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    HANDLE hProcess = NULL;

    __try
    {
        PEPROCESS currentProcess = PsGetCurrentProcess();
        if (!currentProcess)
        {
            __leave;
        }

        status = ObOpenObjectByPointer(currentProcess, OBJ_KERNEL_HANDLE,
            NULL, PROCESS_ALL_ACCESS, *PsProcessType, KernelMode,
            &hProcess);
        if (!NT_SUCCESS(status))
        {
            DbgPrint("ObOpenObjectByPointer failed with status 0x%X\n", status);
            __leave;
        }

        status = GetImagePathFromOpenHandle(hProcess, ProcessPath);
        if (!NT_SUCCESS(status))
        {
            DbgPrint("GetImagePathFromOpenHandle failed with status 0x%X\n", status);
            __leave;
        }

    }
    __finally
    {
        if (hProcess)
        {
            ZwClose(hProcess);
        }
    }

    return status;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
* Stuff related to threads
*/

VOID
ThreadFilterNotifyRoutine(
    _In_ HANDLE ProcessId,
    _In_ HANDLE ThreadId,
    _In_ BOOLEAN Create
)
{
    UNREFERENCED_PARAMETER(ProcessId);
    UNREFERENCED_PARAMETER(ThreadId);
    UNREFERENCED_PARAMETER(Create);

    if (!gThreadMonitoringEnabled)
    {
        return;
    }

    PUNICODE_STRING pProcessPath = NULL;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    __try
    {
        //__debugbreak();
        if (Create) {
            //__debugbreak();
            status = GetImagePathFromPid(ProcessId, &pProcessPath);
            if (!NT_SUCCESS(status))
            {
                DbgPrint("GetCurrentProcessImagePath failed with status 0x%X\n", status);
                __leave;
            }
        }
        
        PMY_CUSTOM_MESSAGE msg = ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(MY_CUSTOM_MESSAGE),
            'gsmT'
        );

        if (!msg)
        {
            return;
        }

        LARGE_INTEGER timestamp;
        KeQuerySystemTime(&timestamp);

        RtlZeroMemory(msg, sizeof(*msg));

        if (Create) {
            RtlStringCchPrintfW(
                msg->replyData.message,
                1024,
                L"[%llu] [ThreadCreate] [%lu] [Path: %ws] [Status: %ws]\r\n",
                timestamp.QuadPart,
                HandleToULong(ThreadId),
                pProcessPath->Buffer,
                NT_SUCCESS(status) ? L"Success" : L"Failure"
            );
        }
        else
        {
            RtlStringCchPrintfW(
                msg->replyData.message,
                1024,
                L"[%llu] [ThreadExit] [%lu] [%lu] [Status: %ws]\r\n",
                timestamp.QuadPart,
                HandleToULong(ProcessId),
                HandleToULong(ThreadId),
                L"Success"
            );
		}

        //wcscpy(msg.replyData.message, L"process");

        msg->replyData.messageLength = (ULONG)wcslen(msg->replyData.message) * sizeof(WCHAR);

        TpEnqueueWorkItem(&gThreadPool->tp, SendWorker, msg);
    }
    __finally
    {
        if (pProcessPath)
        {
            ExFreePoolWithTag(pProcessPath, UTILS_TAG_UNICODE_STRING);
        }
    }
}

NTSTATUS ThreadFilterInitialize()
{
    return PsSetCreateThreadNotifyRoutine(ThreadFilterNotifyRoutine);

}

NTSTATUS ThreadFilterUninitialize()
{
    return PsRemoveCreateThreadNotifyRoutine(ThreadFilterNotifyRoutine);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
* Stuff related to registry keys
*/

NTSTATUS
CmRegistryCallback(
    _In_     PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2
)
/*++
    Class                            ||  Structure:
    ===============================================================
    RegNtDeleteKey                   ||  REG_DELETE_KEY_INFORMATION
    RegNtPreDeleteKey                ||  REG_DELETE_KEY_INFORMATION
    RegNtPostDeleteKey               ||  REG_POST_OPERATION_INFORMATION
    RegNtSetValueKey                 ||  REG_SET_VALUE_KEY_INFORMATION
    RegNtPreSetValueKey              ||  REG_SET_VALUE_KEY_INFORMATION
    RegNtPostSetValueKey             ||  REG_POST_OPERATION_INFORMATION
    RegNtDeleteValueKey              ||  REG_DELETE_VALUE_KEY_INFORMATION
    RegNtPreDeleteValueKey           ||  REG_DELETE_VALUE_KEY_INFORMATION
    RegNtPostDeleteValueKey          ||  REG_POST_OPERATION_INFORMATION
    RegNtSetInformationKey           ||  REG_SET_INFORMATION_KEY_INFORMATION
    RegNtPreSetInformationKey        ||  REG_SET_INFORMATION_KEY_INFORMATION
    RegNtPostSetInformationKey       ||  REG_POST_OPERATION_INFORMATION
    RegNtRenameKey                   ||  REG_RENAME_KEY_INFORMATION
    RegNtPreRenameKey                ||  REG_RENAME_KEY_INFORMATION
    RegNtPostRenameKey               ||  REG_POST_OPERATION_INFORMATION
    RegNtEnumerateKey                ||  REG_ENUMERATE_KEY_INFORMATION
    RegNtPreEnumerateKey             ||  REG_ENUMERATE_KEY_INFORMATION
    RegNtPostEnumerateKey            ||  REG_POST_OPERATION_INFORMATION
    RegNtEnumerateValueKey           ||  REG_ENUMERATE_VALUE_KEY_INFORMATION
    RegNtPreEnumerateValueKey        ||  REG_ENUMERATE_VALUE_KEY_INFORMATION
    RegNtPostEnumerateValueKey       ||  REG_POST_OPERATION_INFORMATION
    RegNtQueryKey                    ||  REG_QUERY_KEY_INFORMATION
    RegNtPreQueryKey                 ||  REG_QUERY_KEY_INFORMATION
    RegNtPostQueryKey                ||  REG_POST_OPERATION_INFORMATION
    RegNtQueryValueKey               ||  REG_QUERY_VALUE_KEY_INFORMATION
    RegNtPreQueryValueKey            ||  REG_QUERY_VALUE_KEY_INFORMATION
    RegNtPostQueryValueKey           ||  REG_POST_OPERATION_INFORMATION
    RegNtQueryMultipleValueKey       ||  REG_QUERY_MULTIPLE_VALUE_KEY_INFORMATION
    RegNtPreQueryMultipleValueKey    ||  REG_QUERY_MULTIPLE_VALUE_KEY_INFORMATION
    RegNtPostQueryMultipleValueKey   ||  REG_POST_OPERATION_INFORMATION
    RegNtPreCreateKey                ||  REG_PRE_CREATE_KEY_INFORMATION
    RegNtPreCreateKeyEx              ||  REG_CREATE_KEY_INFORMATION**
    RegNtPostCreateKey               ||  REG_POST_CREATE_KEY_INFORMATION
    RegNtPostCreateKeyEx             ||  REG_POST_OPERATION_INFORMATION
    RegNtPreOpenKey                  ||  REG_PRE_OPEN_KEY_INFORMATION**
    RegNtPreOpenKeyEx                ||  REG_OPEN_KEY_INFORMATION
    RegNtPostOpenKey                 ||  REG_POST_OPEN_KEY_INFORMATION
    RegNtPostOpenKeyEx               ||  REG_POST_OPERATION_INFORMATION
    RegNtKeyHandleClose              ||  REG_KEY_HANDLE_CLOSE_INFORMATION
    RegNtPreKeyHandleClose           ||  REG_KEY_HANDLE_CLOSE_INFORMATION
    RegNtPostKeyHandleClose          ||  REG_POST_OPERATION_INFORMATION
    RegNtPreFlushKey                 ||  REG_FLUSH_KEY_INFORMATION
    RegNtPostFlushKey                ||  REG_POST_OPERATION_INFORMATION
    RegNtPreLoadKey                  ||  REG_LOAD_KEY_INFORMATION
    RegNtPostLoadKey                 ||  REG_POST_OPERATION_INFORMATION
    RegNtPreUnLoadKey                ||  REG_UNLOAD_KEY_INFORMATION
    RegNtPostUnLoadKey               ||  REG_POST_OPERATION_INFORMATION
    RegNtPreQueryKeySecurity         ||  REG_QUERY_KEY_SECURITY_INFORMATION
    RegNtPostQueryKeySecurity        ||  REG_POST_OPERATION_INFORMATION
    RegNtPreSetKeySecurity           ||  REG_SET_KEY_SECURITY_INFORMATION
    RegNtPostSetKeySecurity          ||  REG_POST_OPERATION_INFORMATION
    RegNtCallbackObjectContextCleanup||  REG_CALLBACK_CONTEXT_CLEANUP_INFORMATION
    RegNtPreRestoreKey               ||  REG_RESTORE_KEY_INFORMATION
    RegNtPostRestoreKey              ||  REG_RESTORE_KEY_INFORMATION
    RegNtPreSaveKey                  ||  REG_SAVE_KEY_INFORMATION
    RegNtPostSaveKey                 ||  REG_SAVE_KEY_INFORMATION
    RegNtPreReplaceKey               ||  REG_REPLACE_KEY_INFORMATION
    RegNtPostReplaceKey              ||  REG_REPLACE_KEY_INFORMATION
    RegNtPostCreateKeyEx             ||  REG_POST_OPERATION_INFORMATION
--*/
{
    UNREFERENCED_PARAMETER(CallbackContext); // not using a context yet

    REG_NOTIFY_CLASS regNotifyClass = (REG_NOTIFY_CLASS)(SIZE_T)Argument1;
    PVOID pParameters = Argument2;
    PVOID object = NULL;

    if (!gRegistryMonitoringEnabled)
    {
        return STATUS_SUCCESS;
    }

    WCHAR regOperation[256] = { 0 };
    switch (regNotifyClass)
    {
    case RegNtPreSetValueKey:
        object = ((PREG_SET_VALUE_KEY_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegSetValueKey");
        break;
    case RegNtPostSetValueKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
        wcscpy(regOperation, L"RegSetValueKey");
        break;
    case RegNtPreDeleteValueKey:
        object = ((PREG_DELETE_VALUE_KEY_INFORMATION)pParameters)->Object;
        wcscpy(regOperation, L"RegDeleteValueKey");
        break;
    case RegNtPostDeleteValueKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegDeleteValueKey");
        break;
    case RegNtPreDeleteKey:
        object = ((PREG_DELETE_KEY_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegDeleteKey");
        break;
    case RegNtPostDeleteKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegDeleteKey");
        break;
    case RegNtPostLoadKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegLoadKey");
        break;
    case RegNtPostUnLoadKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegUnLoadKey");
        break;
    case RegNtPreRenameKey:
        object = ((PREG_RENAME_KEY_INFORMATION)pParameters)->Object;
		wcscat(regOperation, L"RegRenameKey");
        break;
    case RegNtPostRenameKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
		wcscat(regOperation, L"RegRenameKey");
        break;
    case RegNtPostQueryValueKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
        wcscpy(regOperation, L"RegQueryValueKey");
        break;
    case RegNtPostCreateKeyEx:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
        wcscpy(regOperation, L"RegCreateKeyEx");
        break;
    case RegNtPostOpenKeyEx:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
        wcscpy(regOperation, L"RegOpenKeyEx");
        break;
    case RegNtPreSaveKey:
        object = ((PREG_SAVE_KEY_INFORMATION)pParameters)->Object;
        wcscpy(regOperation, L"RegSaveKey");
        break;

    case RegNtPostSaveKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
        wcscpy(regOperation, L"RegSaveKey");
        break;
    case RegNtPreQueryValueKey:
        object = ((PREG_QUERY_VALUE_KEY_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegQueryValueKey");
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
		wcscpy(regOperation, L"RegCreateKey");
        break;
    case RegNtPreSetInformationKey:
        object = ((PREG_SET_INFORMATION_KEY_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegSetInformationKey");
        break;
    case RegNtPostSetInformationKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
        wcscpy(regOperation, L"RegSetInformationKey");
        break;
    case RegNtPreEnumerateKey:
        object = ((PREG_ENUMERATE_KEY_INFORMATION)pParameters)->Object;
        wcscpy(regOperation, L"RegEnumerateKey");
        break;
    case RegNtPostEnumerateKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegEnumerateKey");
        break;
    case RegNtPreEnumerateValueKey:
        object = ((PREG_ENUMERATE_VALUE_KEY_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegEnumerateValueKey");
        break;
    case RegNtPostEnumerateValueKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegEnumerateValueKey");
        break;
    case RegNtPreQueryKey:
        object = ((PREG_QUERY_KEY_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegQueryKey");
        break;
    case RegNtPostQueryKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegQueryKey");
        break;
    case RegNtPreQueryMultipleValueKey:
        object = ((PREG_QUERY_MULTIPLE_VALUE_KEY_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegQueryMultipleValueKey");
        break;
    case RegNtPostQueryMultipleValueKey:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegQueryMultipleValueKey");
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
		wcscpy(regOperation, L"RegOpenKey");
        break;
   /* case RegNtPreKeyHandleClose:
        object = ((PREG_KEY_HANDLE_CLOSE_INFORMATION)pParameters)->Object;
		wcscpy(regOperation, L"RegKeyHandleClose");
        break;
    case RegNtPostKeyHandleClose:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
        wcscpy(regOperation, L"RegKeyHandleClose");
        break;*/
    case RegNtPreLoadKey:
        object = ((PREG_LOAD_KEY_INFORMATION)pParameters)->Object;
        break;
    case RegNtPreUnLoadKey:
        object = ((PREG_UNLOAD_KEY_INFORMATION)pParameters)->Object;
        break;
  /*  case RegNtPreQueryKeySecurity:
        object = ((PREG_QUERY_KEY_SECURITY_INFORMATION)pParameters)->Object;
        break;
    case RegNtPostQueryKeySecurity:
        object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
        break;*/
    //case RegNtPreSetKeySecurity:
    //    object = ((PREG_SET_KEY_SECURITY_INFORMATION)pParameters)->Object;
    //    break;
    //case RegNtPostSetKeySecurity:
    //    object = ((PREG_POST_OPERATION_INFORMATION)pParameters)->Object;
    //    break;
   /* case RegNtCallbackObjectContextCleanup:
        object = ((PREG_CALLBACK_CONTEXT_CLEANUP_INFORMATION)pParameters)->Object;
        break;
    case RegNtPreRestoreKey:
        object = ((PREG_RESTORE_KEY_INFORMATION)pParameters)->Object;
        break;
    case RegNtPostRestoreKey:
        object = ((PREG_RESTORE_KEY_INFORMATION)pParameters)->Object;
        break;*/
 /*   case RegNtPreReplaceKey:
        object = ((PREG_REPLACE_KEY_INFORMATION)pParameters)->Object;
        break;
    case RegNtPostReplaceKey:
        object = ((PREG_REPLACE_KEY_INFORMATION)pParameters)->Object;
        break;*/
    default:
        break;
    }

    if (regNotifyClass == RegNtQueryValueKey ||
        regNotifyClass == RegNtPreQueryValueKey ||
        regNotifyClass == RegNtPostQueryValueKey)
    {
        // registry query is too spammy to display in debugger
        return STATUS_SUCCESS;
    }

    if (object)
    {
        ULONG_PTR objectId;
        PUNICODE_STRING objectName;
        NTSTATUS status = STATUS_UNSUCCESSFUL;
        status = CmCallbackGetKeyObjectIDEx(&gRegistryCookie, object, &objectId, &objectName, 0);
        if (!NT_SUCCESS(status))
        {
            DbgPrint("CmCallbackGetKeyObjectIDEx failed with status = 0x%X\r\n", status);
        }
        else
        {
            PMY_CUSTOM_MESSAGE msg = ExAllocatePool2(
                POOL_FLAG_NON_PAGED,
                sizeof(MY_CUSTOM_MESSAGE),
                'gsmT'
            );

            if (!msg)
            {
                return STATUS_UNSUCCESSFUL;
            }

            LARGE_INTEGER timestamp;
            KeQuerySystemTime(&timestamp);

            RtlZeroMemory(msg, sizeof(*msg));

            UNICODE_STRING regOperationString;
            RtlInitUnicodeString(&regOperationString, regOperation);

            RtlStringCchPrintfW(
                msg->replyData.message,
                1024,
                L"[%llu] [%wZ] Key=%wZ\r\n",
				timestamp.QuadPart,
                &regOperationString,
                objectName
            );

            //wcscpy(msg.replyData.message, L"process");

            msg->replyData.messageLength = (ULONG)wcslen(msg->replyData.message) * sizeof(WCHAR);

            TpEnqueueWorkItem(&gThreadPool->tp, SendWorker, msg);
            CmCallbackReleaseKeyObjectIDEx(objectName);
        }
    }

    return STATUS_SUCCESS;
}


NTSTATUS RegistryFilterInitialize()
{
    return CmRegisterCallbackEx(CmRegistryCallback,
        &gAltitude, gDriverObject, NULL, &gRegistryCookie, NULL);
}

NTSTATUS RegistryFilterUninitialize()
{
    return CmUnRegisterCallback(gRegistryCookie);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
* Stuff related to processes
*/

void
ProcFltSendMessageProcessCreate(
    HANDLE ProcessId,
    PPS_CREATE_NOTIFY_INFO CreateInfo
)
{
	UNREFERENCED_PARAMETER(CreateInfo);
	UNREFERENCED_PARAMETER(ProcessId);

    PMY_CUSTOM_MESSAGE msg = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(MY_CUSTOM_MESSAGE),
        'gsmT'
    );

    if (!msg)
    {
        return;
    }

    LARGE_INTEGER timestamp;
    KeQuerySystemTime(&timestamp);

    RtlZeroMemory(msg, sizeof(*msg));

    RtlStringCchPrintfW(
        msg->replyData.message,
        1024,
        L"[%llu] [ProcessCreate] [%lu] [Path=%wZ] [Status=%ws] [Cmd=%wZ]\r\n",
        timestamp.QuadPart,
        HandleToULong(ProcessId),
        CreateInfo->ImageFileName,
        CreateInfo->CreationStatus == STATUS_SUCCESS ? L"Success\0" : L"Failure\0",
        CreateInfo->CommandLine
    );

	//wcscpy(msg.replyData.message, L"process");

    msg->replyData.messageLength = (ULONG)wcslen(msg->replyData.message) * sizeof(WCHAR);

    TpEnqueueWorkItem(&gThreadPool->tp, SendWorker, msg);
}

static VOID
ProcFltSendMessageProcessTerminate(
    _In_ HANDLE ProcessId
)
{
    UNREFERENCED_PARAMETER(ProcessId);

    if (!gClientPort)
    {
        return;
    }

    PMY_CUSTOM_MESSAGE msg = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(MY_CUSTOM_MESSAGE),
        'gsmT'
    );

    if (!msg)
    {
        return;
    }

    LARGE_INTEGER timestamp;
    KeQuerySystemTime(&timestamp);

    RtlZeroMemory(msg, sizeof(*msg));

    //__debugbreak();

    RtlStringCchPrintfW(
        msg->replyData.message,
        1024,
        L"[%llu] [ProcessTerminate] [%lu]\r\n",
        timestamp.QuadPart,
        HandleToULong(ProcessId)
    );

    //wcscpy(msg.replyData.message, L"process");

    msg->replyData.messageLength = (ULONG)wcslen(msg->replyData.message) * sizeof(WCHAR);

    TpEnqueueWorkItem(&gThreadPool->tp, SendWorker, msg);
}

VOID
ProcessFltNotifyRoutine(
	PEPROCESS Process,
	HANDLE ProcessId,
	PPS_CREATE_NOTIFY_INFO CreateInfo
)
{
	UNREFERENCED_PARAMETER(Process);

    if (!gProcessMonitoringEnabled)
    {
        return;
    }

    if (CreateInfo)
    {
        //__debugbreak();
        ProcFltSendMessageProcessCreate(ProcessId, CreateInfo);
    }
    else
    {
        ProcFltSendMessageProcessTerminate(ProcessId);
    }
}

NTSTATUS
ProcessFilterInitialize()
{
    return PsSetCreateProcessNotifyRoutineEx(ProcessFltNotifyRoutine, FALSE);
}

NTSTATUS
ProcessFilterUninitialize()
{
    return PsSetCreateProcessNotifyRoutineEx(ProcessFltNotifyRoutine, TRUE);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
* Stuff related to images
*/

VOID
PLoadImageNotifyRoutine(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo
)
{
    UNREFERENCED_PARAMETER(ProcessId);
    UNREFERENCED_PARAMETER(FullImageName);

    //__debugbreak();

    /*if (!ImageInfo->SystemModeImage)
    {
        return;
    }*/

    if (gClientPort == NULL)
    {
        DbgPrintEx(0, 0, "No client connected to receive messages.\n");
        return;
    }

    PIMAGE_INFO_EX imgInfo = CONTAINING_RECORD(ImageInfo, IMAGE_INFO_EX, ImageInfo);

    UNICODE_STRING imageFileName;
    RtlInitUnicodeString(&imageFileName, imgInfo->FileObject->FileName.Buffer);
    //imageFileName->Length = imgInfo->FileObject->FileName.Length;
    //imageFileName->MaximumLength = imgInfo->FileObject->FileName.MaximumLength;
    //imageFileName->Buffer = ExAllocatePool2(POOL_FLAG_PAGED, imageFileName->MaximumLength, 'imN');

    if (imageFileName.Buffer != NULL)
    {
        //__debugbreak();

        //RtlCopyMemory(imageFileName->Buffer, imgInfo->FileObject->FileName.Buffer, imageFileName->Length);
        PMY_CUSTOM_MESSAGE msg = ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(MY_CUSTOM_MESSAGE),
            'gsmT'
        );

        if (!msg)
        {
            return;
        }

        LARGE_INTEGER timestamp;
        KeQuerySystemTime(&timestamp);

        RtlZeroMemory(msg, sizeof(*msg));

        RtlStringCchPrintfW(
            msg->replyData.message,
            1024,
            L"[%llu] [ImageLoad] [Path: %wZ] [Type: %wZ]\r\n",
            timestamp.QuadPart,
            &imageFileName,
            ImageInfo->SystemModeImage ? L"System" : L"User"
        );

        //wcscpy(msg.replyData.message, L"process");

        msg->replyData.messageLength = (ULONG)wcslen(msg->replyData.message) * sizeof(WCHAR);

        TpEnqueueWorkItem(&gThreadPool->tp, SendWorker, msg);
    }
}

VOID
ImageFltNotifyRoutine(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo
)
{
    //__debugbreak();

    if (!gImageMonitoringEnabled)
    {
        return;
    }

    PLoadImageNotifyRoutine(FullImageName, ProcessId, ImageInfo);
}

NTSTATUS
ImageFilterInitialize()
{
    return PsSetLoadImageNotifyRoutine(ImageFltNotifyRoutine);
}

NTSTATUS
ImageFilterUninitialize()
{
    return PsRemoveLoadImageNotifyRoutine(ImageFltNotifyRoutine);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
* File operation callbacks
*/

CONST FLT_OPERATION_REGISTRATION callbacks[] = {
    { IRP_MJ_CREATE, 0, PreCreate, NULL },

    //{ IRP_MJ_CLOSE, 0, , NULL },

    { IRP_MJ_OPERATION_END }
};

FLT_PREOP_CALLBACK_STATUS
PreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Out_ PVOID* Buffer
)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Buffer);

    if (!gClientPort || !gFileMonitoringEnabled)
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;

    if (NT_SUCCESS(FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED, &nameInfo)))
    {
        //__debugbreak();

        FltParseFileNameInformation(nameInfo);

        PMY_CUSTOM_MESSAGE msg = ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(MY_CUSTOM_MESSAGE),
            'gsmT'
        );

        if (!msg)
        {
			WikddLogError("Failed to allocate memory for message.");
            return FLT_PREOP_COMPLETE;
        }

        LARGE_INTEGER timestamp;
        KeQuerySystemTime(&timestamp);

        RtlZeroMemory(msg, sizeof(*msg));

        UNICODE_STRING fileName;
        RtlInitUnicodeString(&fileName, nameInfo->Name.Buffer);

        RtlStringCchPrintfW(
            msg->replyData.message,
            1024,
            L"[%llu] [CreateFile] [%wZ]\r\n",
            timestamp.QuadPart,
            &fileName
            //HandleToULong(ProcessId)
        );

        FltReleaseFileNameInformation(nameInfo);

        //wcscpy(msg.replyData.message, L"process");

        msg->replyData.messageLength = (ULONG)wcslen(msg->replyData.message) * sizeof(WCHAR);

        TpEnqueueWorkItem(&gThreadPool->tp, SendWorker, msg);
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
* Callbacks for communication with user-mode application
*/

NTSTATUS
ConnectNotifyCallback(
    PFLT_PORT ClientPort,
    PVOID ServerPortCookie,
    PVOID ConnectionContext,
    ULONG SizeOfContext,
    PVOID* ConnectionPortCookie
)
{
    //__debugbreak();

    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);
    UNREFERENCED_PARAMETER(ConnectionPortCookie);

    gClientPort = ClientPort;

    return STATUS_SUCCESS;
}

VOID
DisconnectNotifyCallback(
    PVOID ConnectionCookie
)
{
    UNREFERENCED_PARAMETER(ConnectionCookie);

    if (gClientPort)
    {
        FltCloseClientPort(gFilterRegistration, &gClientPort);
        gClientPort = NULL;
    }
}

wchar_t* FindNextToken(wchar_t* str, wchar_t delimiter)
{
    while (*str && *str != delimiter)
    {
        str++;
    }

    if (*str == delimiter)
    {
        *str++ = L'\0';
    }

    return str;
}

VOID
HandleUserMessage(WCHAR* message, ULONG messageLength)
{
    UNREFERENCED_PARAMETER(message);
    UNREFERENCED_PARAMETER(messageLength);

    __debugbreak();
    
    BOOLEAN processMonitoringFlag = FALSE;
	BOOLEAN imageMonitoringFlag = FALSE;
    BOOLEAN threadMonitoringFlag = FALSE;
	BOOLEAN registryMonitoringFlag = FALSE;
    BOOLEAN fileMonitoringEnabled = FALSE;

    WCHAR* current = message;

    while (*current)
    {
		WCHAR* next = FindNextToken(current, L' ');

        if (wcsncmp(current, L"process", wcslen(L"process")) == 0)
        {
            processMonitoringFlag = TRUE;
        }
        else if (wcsncmp(current, L"image", wcslen(L"image")) == 0)
        {
            imageMonitoringFlag = TRUE;
        }
        else if (wcsncmp(current, L"thread", wcslen(L"thread")) == 0)
        {
            threadMonitoringFlag = TRUE;
        }
        else if (wcsncmp(current, L"registry", wcslen(L"registry")) == 0)
        {
            registryMonitoringFlag = TRUE;
        }
        else if (wcsncmp(current, L"file", wcslen(L"file")) == 0)
        {
            fileMonitoringEnabled = TRUE;
		}

        current = next;
    }

	if (processMonitoringFlag)
    {
        if (gProcessMonitoringEnabled == FALSE)
        {
            gProcessMonitoringEnabled = TRUE;
        }
        else
        {
			gProcessMonitoringEnabled = FALSE;
        }
    }


    if (imageMonitoringFlag)
    {
        if (gImageMonitoringEnabled == FALSE)
        {
            gImageMonitoringEnabled = TRUE;
        }
        else
        {
            gImageMonitoringEnabled = FALSE;
		}
    }

    if (threadMonitoringFlag)
    {
        if (gThreadMonitoringEnabled == FALSE)
        {
            gThreadMonitoringEnabled = TRUE;
        }
        else
        {
            gThreadMonitoringEnabled = FALSE;
		}
    }

    if (registryMonitoringFlag)
    {
        if (gRegistryMonitoringEnabled == FALSE)
        {
            gRegistryMonitoringEnabled = TRUE;
        }
        else
        {
            gRegistryMonitoringEnabled = FALSE;
        }
    }

    if (fileMonitoringEnabled)
    {
        if (gFileMonitoringEnabled == FALSE)
        {
            gFileMonitoringEnabled = TRUE;
        }
        else
        {
            gFileMonitoringEnabled = FALSE;
        }
    }
}

NTSTATUS
MessageNotifyCallback(
    PVOID PortCookie,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength,
    PULONG ReturnOutputBufferLength
)
{
    UNREFERENCED_PARAMETER(ReturnOutputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(OutputBuffer);

    __debugbreak();

    UNREFERENCED_PARAMETER(PortCookie);

    if (InputBufferLength < sizeof(CHAR))
    {
        return STATUS_INVALID_PARAMETER;
    }

    PMY_CUSTOM_MESSAGE msgStruct = (PMY_CUSTOM_MESSAGE)(InputBuffer);
    //ULONG charCount = msgStruct->messageLength / sizeof(WCHAR);
    //DbgPrint("Received message from user-mode: %.*ws\n", charCount, msgStruct->message);

    HandleUserMessage(msgStruct->replyData.message, msgStruct->replyData.messageLength);

    /*if (OutputBuffer && OutputBufferLength >= sizeof("ACK"))
    {
        RtlCopyMemory(OutputBuffer, "ACK", sizeof("ACK"));
        *ReturnOutputBufferLength = sizeof("ACK");
    }*/

    return STATUS_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
* Communication & Threadpool stuff
*/

VOID SendWorker(PVOID ctx)
{
    PMY_CUSTOM_MESSAGE msg = (PMY_CUSTOM_MESSAGE)ctx;

    KIRQL oldIrql;
    PFLT_PORT port = NULL;

    KeAcquireSpinLock(&gClientPortLock, &oldIrql);
    port = gClientPort;
    KeReleaseSpinLock(&gClientPortLock, oldIrql);

    LARGE_INTEGER timeout;
    timeout.QuadPart = -10 * 1000 * 1000;

    if (port)
    {
        NTSTATUS status = FltSendMessage(
            gFilterRegistration,
            &port,
            &msg->replyData,
            sizeof(REPLY_DATA),
            NULL,
            NULL,
            &timeout
        );

        if (!NT_SUCCESS(status))
        {
            DbgPrintEx(0, 0, "FltSendMessage failed: 0x%X\n", status);
        }
    }

    ExFreePoolWithTag(msg, 'gsmT');
}

void InitThreadPool(PMY_THREADPOOL threadpool)
{
    hasBeenInitialized = TRUE;

    RtlZeroMemory(&threadpool->tp, sizeof(threadpool->tp));
    RtlZeroMemory(&threadpool->ctx, sizeof(threadpool->ctx));

    NTSTATUS status = TpInit(&threadpool->tp, 5);
    if (!NT_SUCCESS(status))
    {

    }

    KeInitializeSpinLock(&threadpool->ctx.ContextLock);
    threadpool->ctx.Number = 0;
}

NTSTATUS
CreateCommunicationPort()
{
    //__debugbreak();

    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING portName = RTL_CONSTANT_STRING(L"\\MyFilterPort");

    PSECURITY_DESCRIPTOR sd;
    FltBuildDefaultSecurityDescriptor(&sd, FLT_PORT_ALL_ACCESS);

    InitializeObjectAttributes(
        &objAttr,
        &portName,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
        NULL,
        sd
    );

    NTSTATUS status = FltCreateCommunicationPort(
        gFilterRegistration,
        &gServerPort,
        &objAttr,
        NULL,
        ConnectNotifyCallback,
        DisconnectNotifyCallback,
        MessageNotifyCallback,
        1
    );

    FltFreeSecurityDescriptor(sd);

    return status;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
* Driver entry and unload
*/

CONST FLT_REGISTRATION fltReg = {

    sizeof(FLT_REGISTRATION),           //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags

    NULL,                               //  Context
    callbacks,                          //  Operation callbacks

    FilterUnload,                       //  MiniFilterUnload

    NULL,                               //  InstanceSetup
    NULL,                               //  InstanceQueryTeardown
    NULL,                               //  InstanceTeardownStart
    NULL,                               //  InstanceTeardownComplete

    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent
};

NTSTATUS
FilterUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
{
    WikddLogInfo("Unloading driver. Flags = 0x%x", Flags);


    ThreadFilterUninitialize();
    RegistryFilterUninitialize();
    ImageFilterUninitialize();
    ProcessFilterUninitialize();

    if (gThreadPool)
    {
        TpUninit(&gThreadPool->tp);
        ExFreePoolWithTag(gThreadPool, 'ptmT');
        gThreadPool = NULL;
    }

    if (gServerPort)
    {
        FltCloseCommunicationPort(gServerPort);
        gServerPort = NULL;
    }

    if (gFilterRegistration)
    {
        FltUnregisterFilter(gFilterRegistration);
        WPP_CLEANUP(gDriverObject);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    __debugbreak();

    gDriverObject = DriverObject;

    WPP_INIT_TRACING(DriverObject, RegistryPath);

    WikddLogInfo("Starting driver ...");

    UNICODE_STRING altitude = RTL_CONSTANT_STRING(L"389999");
    gAltitude = altitude;

	UNICODE_STRING ustrFunctioName = RTL_CONSTANT_STRING(L"ZwQueryInformationProcess");
	pfnZwQueryInformationProcess = (PFUNC_ZwQueryInformationProcess)(SIZE_T)MmGetSystemRoutineAddress(&ustrFunctioName);
    if (!pfnZwQueryInformationProcess)
    {
        __debugbreak();
        WikddLogError("Failed to get address of ZwQueryInformationProcess");
        return STATUS_UNSUCCESSFUL;
	}

    NTSTATUS status = FltRegisterFilter(
        DriverObject,
        &fltReg,
        &gFilterRegistration
    );
    if (!NT_SUCCESS(status))
    {
        WikddLogApiFailedNt(status, "FltRegisterFilter");
        return status;
    }

    gThreadPool = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MY_THREADPOOL), 'ptmT');
    if (!gThreadPool)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(gThreadPool, sizeof(MY_THREADPOOL));

    InitThreadPool(gThreadPool);

    status = CreateCommunicationPort();
    if (!NT_SUCCESS(status))
    {
        WikddLogApiFailedNt(status, "CreateCommunicationPort");
        FltUnregisterFilter(gFilterRegistration);
        WPP_CLEANUP(gDriverObject);
        return status;
    }

    status = ProcessFilterInitialize();
        
    if (!NT_SUCCESS(status))
    {
        WikddLogApiFailedNt(status, "PsSetCreateProcessNotifyRoutineEx");
        ProcessFilterUninitialize();
        FltUnregisterFilter(gFilterRegistration);
        WPP_CLEANUP(gDriverObject);
        return status;
	}

    status = ImageFilterInitialize();
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(0, 0, "Failed to create load image notify routine (0x%08X)\n", status);
        ImageFilterUninitialize();
		FltUnregisterFilter(gFilterRegistration);
        return status;
    }

	status = ThreadFilterInitialize();
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(0, 0, "Failed to create thread notify routine (0x%08X)\n", status);
        ThreadFilterUninitialize();
    }

    status = RegistryFilterInitialize();
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(0, 0, "Failed to create registry callback (0x%08X)\n", status);
        RegistryFilterUninitialize();
	}

    return FltStartFiltering(gFilterRegistration);
}
