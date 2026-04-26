#include <fltKernel.h>
#include <wdm.h>

#include "process_filter.h"

#include "trace.h"
#include "main.tmh"

PFLT_FILTER gFilterRegistration = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;
PDRIVER_OBJECT gDriverObject = NULL;

DRIVER_INITIALIZE DriverEntry;

typedef struct _MY_CUSTOM_MESSAGE
{
    WCHAR message[512];
    ULONG messageLength;
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

    if (strcmp(message, L"process") == 0)
    {
        ProcessFilterInitialize();
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
    __debugbreak();

    UNREFERENCED_PARAMETER(PortCookie);

    if (InputBufferLength < sizeof(CHAR))
    {
        return STATUS_INVALID_PARAMETER;
    }

    PMY_CUSTOM_MESSAGE msgStruct = (PMY_CUSTOM_MESSAGE)((CHAR*)InputBuffer + sizeof(FILTER_MESSAGE_HEADER));
    //ULONG charCount = msgStruct->messageLength / sizeof(WCHAR);
    //DbgPrint("Received message from user-mode: %.*ws\n", charCount, msgStruct->message);

    HandleUserMessage(msgStruct->message, msgStruct->messageLength);

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
    {IRP_MJ_CREATE, 0, PreCreate, NULL},

    { IRP_MJ_OPERATION_END }
};

FLT_PREOP_CALLBACK_STATUS PreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Out_ PVOID* Buffer
)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Buffer);


    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    MY_CUSTOM_MESSAGE msg;

    if (gClientPort && NT_SUCCESS(FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED, &nameInfo)))
    {

        //__debugbreak();
        wcscpy_s(msg.message, 512, nameInfo->Name.Buffer);
        msg.messageLength = nameInfo->Name.Length;

        //DbgPrint("%s\n", msg.message);
        //DbgPrint("%d\n", msg.messageLength);

        FltSendMessage(gFilterRegistration, &gClientPort, &msg, sizeof(msg), NULL, NULL, NULL);
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

    return FltStartFiltering(gFilterRegistration);
}
