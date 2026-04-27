#include <fltKernel.h>
#include <wdm.h>
#include "ntstrsafe.h"

#include "trace.h"
#include "main.tmh"

PFLT_FILTER gFilterRegistration = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;
PDRIVER_OBJECT gDriverObject = NULL;

BOOLEAN gProcessMonitoringEnabled = FALSE;
BOOLEAN gImageMonitoringEnabled = FALSE;

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

    MY_CUSTOM_MESSAGE msg = { 0 };

    LARGE_INTEGER timestamp;
    KeQuerySystemTime(&timestamp);

   /* RtlStringCchPrintfW(
        msg.message,
        512,
        L"[%llu] [ProcessCreate] [%lu] Path=%wZ Cmd=%wZ",
        timestamp.QuadPart,
        HandleToULong(ProcessId),
        CreateInfo->ImageFileName,
        CreateInfo->CommandLine
    );*/

	wcscpy(msg.replyData.message, L"process");

    msg.replyData.messageLength = (ULONG)wcslen(msg.replyData.message) * sizeof(WCHAR);

    if (!gClientPort)
    {
        return;
	}


    FltSendMessage(
        gFilterRegistration,
        &gClientPort,
        &msg.replyData,
        sizeof(REPLY_DATA),
        NULL,
        NULL,
        NULL
    );
}

//static VOID
//ProcFltSendMessageProcessTerminate(
//    _In_ HANDLE ProcessId
//)
//{
//    UNICODE_STRING message;
//    ULONG32 msgSize = 4 * PAGE_SIZE;
//    message.Buffer = ExAllocatePoolWithTag(PagedPool, msgSize, 'tag1');
//    if (!message.Buffer)
//    {
//        return;
//    }
//    message.MaximumLength = 4 * PAGE_SIZE;
//    message.Length = 0;
//
//    LARGE_INTEGER timestamp = { 0 };
//    KeQuerySystemTime(&timestamp);
//
//    //PUNICODE_STRING ImageFileName = NULL;
//
//    //CommSendString(&message);
//    FltSendMessage(gFilterRegistration, &gClientPort, &message, sizeof(message), NULL, NULL, NULL);
//    ExFreePoolWithTag(message.Buffer, 'tag1');
//
//}

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
  /*  else
    {
        ProcFltSendMessageProcessTerminate(ProcessId);
    }*/
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

    __debugbreak();

    /*if (!ImageInfo->SystemModeImage)
    {
        return;
    }*/

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

        MY_CUSTOM_MESSAGE msg = { 0 };

        //wcscpy(msg.replyData.message, L"image");
        wcscpy_s(msg.replyData.message, 1024, L"image");

        msg.replyData.messageLength = (ULONG)wcslen(msg.replyData.message) * sizeof(WCHAR);

        if (!gClientPort)
        {
            return;
        }

        LARGE_INTEGER timeout;

        timeout.QuadPart = -10 * 1000 * 1000;

        NTSTATUS status = STATUS_UNSUCCESSFUL;

        if (gClientPort == NULL)
        {
            DbgPrintEx(0, 0, "No client connected to receive messages.\n");
            return;
		}

        status = FltSendMessage(
            gFilterRegistration,
            &gClientPort,
            &msg.replyData,
            sizeof(REPLY_DATA),
            NULL,
            NULL,
            &timeout
        );

        if (!NT_SUCCESS(status))
        {
            DbgPrintEx(0, 0, "Failed to send message to user-mode application. Status: 0x%X\n", status);
		}
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



    //if (ImageInfo->SystemModeImage)
    //{
        PLoadImageNotifyRoutine(FullImageName, ProcessId, ImageInfo);
    //}
    /*  else
      {
          ProcFltSendMessageProcessTerminate(ProcessId);
      }*/
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
    __debugbreak();

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

VOID
HandleUserMessage(const WCHAR* message, ULONG messageLength)
{
    UNREFERENCED_PARAMETER(message);
    UNREFERENCED_PARAMETER(messageLength);
    __debugbreak();
    //DbgPrintEx(0, 0, "Received message from user-mode: %.*ws\n", messageLength / sizeof(WCHAR), message);
    MY_CUSTOM_MESSAGE msg;
	UNREFERENCED_PARAMETER(msg);

	if (wcsncmp(message, L"process", messageLength / sizeof(WCHAR)) == 0)
    {
		gProcessMonitoringEnabled = TRUE;
    }

    if (wcsncmp(message, L"image", messageLength / sizeof(WCHAR)) == 0)
    {
        gImageMonitoringEnabled = TRUE;
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

NTSTATUS
CreateCommunicationPort()
{
    __debugbreak();

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
* File operation callbacks
*/

CONST FLT_OPERATION_REGISTRATION callbacks[] = {
    //{IRP_MJ_CREATE, 0, PreCreate, NULL},

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


    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    MY_CUSTOM_MESSAGE msg = { 0 };

    if (gClientPort && NT_SUCCESS(FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED, &nameInfo)))
    {
        //__debugbreak();

        wcscpy_s(msg.replyData.message, 1024, nameInfo->Name.Buffer);
        msg.replyData.messageLength = nameInfo->Name.Length;

        FltSendMessage(gFilterRegistration, &gClientPort, &msg.replyData, sizeof(REPLY_DATA), NULL, NULL, NULL);
        FltReleaseFileNameInformation(nameInfo);
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
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
        FltUnregisterFilter(gFilterRegistration);
        WPP_CLEANUP(gDriverObject);
        return status;
	}

    status = ImageFilterInitialize();
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(0, 0, "Failed to create load image notify routine (0x%08X)\n", status);
        return status;
    }

    return FltStartFiltering(gFilterRegistration);
}
