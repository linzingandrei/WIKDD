#define _CRTDBG_MAP_ALLOC

#define WIN32_NO_STATUS
#include <Windows.h>
#include <winternl.h>
#include <intsafe.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>

#include <stdio.h>
#include <assert.h>
#include <crtdbg.h>
#include <conio.h>
#include <psapi.h>
#include "intrin.h"
#include <tchar.h>
#include <winioctl.h>
#include "TlHelp32.h"
#include <fltuser.h>

#include "thread_injection.h"

#include "../MainDriver/main.h"
#include "../MainDriver/process_protect_common.h"

#pragma comment(lib, "fltlib.lib")


#include "Trace.h"
#include <stdlib.h>
//#include "x64/Debug/ConsoleApplication1.tmh"

//
// **********************************************************
// *                        LIST API                        *
// **********************************************************
//

void
ListInitializeHead(
    _Inout_ PLIST_ENTRY ListHead
)
{
    /*
     *  ListHead->Flink represent the "front" link or next element.
     *  ListHead->Blink represent the "back" link or previous element.
     *
     *  At first, the list is initialized as being recursive, that's it:
     *      both front link and back link points to the head.
     *
     *      /--BLINK-- [ List Head ] --FLINK--\
     *      |            ^      ^             |
     *      \------------|      |-------------/
     *
     */
    ListHead->Flink = ListHead;
    ListHead->Blink = ListHead;
}

bool
ListIsEmpty(
    _In_ _Const_ const PLIST_ENTRY ListHead
)
{
    /*
     * An empty list is the one where both flink and blink points to the head.
     * See the diagarm in InitializeListHead to understand better.
     */
    return (ListHead->Flink == ListHead) &&
        (ListHead->Blink == ListHead);
}

void
ListInsertHead(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PLIST_ENTRY Element
)
{
    /* List must remain recursive. */
    assert(ListHead->Flink->Blink == ListHead);
    assert(ListHead->Blink->Flink == ListHead);

    /*
     * This:
     *  [blink]--[ListHead]--[flink]
     *
     * Becomes:
     *  [blink]--[ListHead]--[element]--[flink]
     *
     * ListHead is actually a sentinel node used to access the actual head and tail.
     */

     /* flink is actually the head of the list */
    PLIST_ENTRY actualHead = ListHead->Flink;

    /*
     * Actual insertion:
     *  1. [ListHead] <--BLINK-- [Element] --FLINK--> [flink]
     *  2. [ListHead] --FLINK--> [Element] <--BLINK-- [flink]
     */
    Element->Flink = ListHead->Flink;
    Element->Blink = ListHead;

    actualHead->Blink = Element;
    ListHead->Flink = Element;
}

PLIST_ENTRY
ListRemoveTail(
    _Inout_ PLIST_ENTRY ListHead
)
{
    /* List must remain recursive. */
    assert(ListHead->Flink->Blink == ListHead);
    assert(ListHead->Blink->Flink == ListHead);

    /* No element in list. Bail. */
    if (ListIsEmpty(ListHead))
    {
        return nullptr;
    }

    /* Blink is actually the tail of the list. */
    PLIST_ENTRY elementToRemove = ListHead->Blink;

    /* [prevElement] -- [elementToRemove] -- [nextElement] */
    PLIST_ENTRY prevElement = elementToRemove->Blink;
    PLIST_ENTRY nextElement = elementToRemove->Flink;

    /*
     * Detach the element:
     *  [prevElement] --FLINK--> [nextElement]
     *                <--BLINK--
     */
    prevElement->Flink = nextElement;
    nextElement->Blink = prevElement;

    /* Make it a recursive element before returning to caller. Don't expose valid list pointers.*/
    elementToRemove->Blink = elementToRemove;
    elementToRemove->Flink = elementToRemove;

    return elementToRemove;
}

//
// **********************************************************
// *                        TP API                          *
// **********************************************************
//

//
// MY_THREAD_POOL - simple thread pool implementation
//
typedef struct _MY_THREAD_POOL
{
    /* When this event is signaled The threads should stop. */
    HANDLE StopThreadPoolEvent;
    /* This event is used to signal that the threads have work to perform. */
    HANDLE WorkScheduledEvent;
    /* Number of threads in the ThreadHandles array. */
    UINT32 NumberOfThreads;
    /* List of threads started in thread pool. */
    HANDLE* ThreadHandles;
    /* The list of work items and the mutex  protecting. */
    SRWLOCK QueueLock;
    /* Enqueued work items - represented as a double linked list. */
    LIST_ENTRY Queue;
} MY_THREAD_POOL;

//
// MY_WORK_ITEM - A very basic work item
//
typedef struct _MY_WORK_ITEM
{
    /* Required by the MY_THREAD_POOL, so it can be enqueued and dequeued. */
    LIST_ENTRY ListEntry;
    /* Callback to be called. */
    LPTHREAD_START_ROUTINE WorkRoutine;
    /* Caller defined context. To be passed to work routine. */
    PVOID Context;
} MY_WORK_ITEM;

DWORD WINAPI
TpRoutine(
    _In_opt_ PVOID Context
)
{
    MY_THREAD_POOL* threadPool = (MY_THREAD_POOL*)(Context);
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    if (NULL == threadPool)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Wait for work or for stop event. */
    HANDLE waitEvents[2] = { threadPool->StopThreadPoolEvent,
                             threadPool->WorkScheduledEvent };
    while (true)
    {
        /* This will waits for any of the two events. */
        DWORD waitResult = WaitForMultipleObjects(ARRAYSIZE(waitEvents),
            waitEvents,
            FALSE,
            INFINITE);
        /* StopThreadPoolEvent signaled. Graceful exit. */
        if (WAIT_OBJECT_0 == waitResult)
        {
            status = STATUS_SUCCESS;
            break;
        }
        /* WorkScheduledEvent signaled. We start the processing loop. */
        else if (WAIT_OBJECT_0 + 1 == waitResult)
        {
            /* Processing loop. Keep going until we empty the work queue. */
            while (true)
            {
                LIST_ENTRY* listTail = NULL;
                MY_WORK_ITEM* workItem = NULL;

                /* todo: Pop one element from list. This must be done with lock taken. */
                AcquireSRWLockExclusive(&threadPool->QueueLock);
                listTail = ListRemoveTail(&threadPool->Queue);
                if (listTail != NULL)
                {
                    workItem = CONTAINING_RECORD(listTail, MY_WORK_ITEM, ListEntry);
                }
                ReleaseSRWLockExclusive(&threadPool->QueueLock);

                /* If we have an item, we invoke the work routine it and then we free it. */
                if (NULL != workItem)
                {
                    /* todo: call work routine. */
                    /* todo: ensure memory management :) */
                    workItem->WorkRoutine(workItem->Context);
                    free(workItem);
                }

                /* If we didn't managed to get a work item, we stop the processing loop. */
                if (NULL == workItem)
                {
                    break;
                }
            }
        }
        /* An error occured. We stop the thread. */
        else
        {
            status = STATUS_THREADPOOL_HANDLE_EXCEPTION;
            break;
        }
    }

    return status;
}

void
TpUninit(
    _Inout_ MY_THREAD_POOL* ThreadPool
)
{
    /* Nothing to do. */
    if (NULL == ThreadPool)
    {
        return;
    }

    /* First set the stop event, if any. */
    if (NULL != ThreadPool->StopThreadPoolEvent)
    {
        /* todo: signal stop event. */
        if (!SetEvent(ThreadPool->StopThreadPoolEvent))
        {
            printf("SetEvent failed (%d)\n", GetLastError());
            return;
        }
    }

    /* Now wait for threads. */
    if (NULL != ThreadPool->ThreadHandles)
    {
        for (UINT32 i = 0; i < ThreadPool->NumberOfThreads; ++i)
        {
            if (NULL != ThreadPool->ThreadHandles[i])
            {
                /* todo wait for thread. */
                /* todo close thread handle. */
                DWORD res = WaitForSingleObject(ThreadPool->ThreadHandles[i], INFINITE);
                if (WAIT_FAILED == res)
                {
                    printf("WaitForMultipleObjects failed (%d)\n", GetLastError());
                }

                CloseHandle(ThreadPool->ThreadHandles[i]);
            }
        }
        free(ThreadPool->ThreadHandles);
    }
    ThreadPool->ThreadHandles = NULL;
    ThreadPool->NumberOfThreads = 0;

    /* todo: empty the work queue and process the rest of the work items take care of synchronization! */
    while (!ListIsEmpty(&ThreadPool->Queue))
    {
        AcquireSRWLockExclusive(&ThreadPool->QueueLock);
        LIST_ENTRY* entry = ListRemoveTail(&ThreadPool->Queue);
        MY_WORK_ITEM* work = CONTAINING_RECORD(entry, MY_WORK_ITEM, WorkRoutine);
        work->WorkRoutine(work->Context);
        free(work);
        AcquireSRWLockExclusive(&ThreadPool->QueueLock);
    }

    /* And close the event handles. */
    if (NULL != ThreadPool->StopThreadPoolEvent)
    {
        CloseHandle(ThreadPool->StopThreadPoolEvent);
        ThreadPool->StopThreadPoolEvent = NULL;
    }
    if (NULL != ThreadPool->WorkScheduledEvent)
    {
        CloseHandle(ThreadPool->WorkScheduledEvent);
        ThreadPool->WorkScheduledEvent = NULL;
    }
}

NTSTATUS
TpInit(
    _Inout_ MY_THREAD_POOL* ThreadPool,
    _In_ UINT32 NumberOfThreads
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    HRESULT hRes = 0;
    UINT32 requiredSizeForThreads = 0;

    /* Sanity checks for parameters. */
    if (NULL == ThreadPool || 0 == NumberOfThreads)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Preinit Threadpool with zeroes. */
    RtlZeroMemory(ThreadPool, sizeof(MY_THREAD_POOL));

    /* Initialize the work queue. */
    ListInitializeHead(&ThreadPool->Queue);
    InitializeSRWLock(&ThreadPool->QueueLock);

    /* Initialize stop event - once set, this will remain signaled as it needs to notify all threads. */
    ThreadPool->StopThreadPoolEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (NULL == ThreadPool->StopThreadPoolEvent)
    {
        status = STATUS_INVALID_HANDLE;
        goto CleanUp;
    }

    /* Initialize work notified event - this is an auto reset event as it needs to wake one thread at a time. */
    ThreadPool->WorkScheduledEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (NULL == ThreadPool->WorkScheduledEvent)
    {
        status = STATUS_INVALID_HANDLE;
        goto CleanUp;
    }

    /* Now allocate space for the threads. */
    hRes = UInt32Mult(sizeof(HANDLE), NumberOfThreads, &requiredSizeForThreads);
    if (!SUCCEEDED(hRes))
    {
        status = STATUS_INTEGER_OVERFLOW;
        goto CleanUp;
    }
    ThreadPool->ThreadHandles = (HANDLE*)malloc(requiredSizeForThreads);
    if (NULL == ThreadPool->ThreadHandles)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto CleanUp;
    }
    RtlZeroMemory(ThreadPool->ThreadHandles, requiredSizeForThreads);

    /* And finally create the actual threads - they will start executing TpRoutine. */
    for (UINT32 i = 0; i < NumberOfThreads; ++i)
    {
        /* todo: create a single thread with TpRoutine as work and pass ThreadPool as context. */
        ThreadPool->ThreadHandles[i] = CreateThread(NULL, 0, TpRoutine, ThreadPool, 0, NULL);

        if (NULL == ThreadPool->ThreadHandles[i])
        {
            status = STATUS_INVALID_HANDLE;
            goto CleanUp;
        }
        /* We created one more thread successfully. */
        ThreadPool->NumberOfThreads++;
    }

    /* All good. */
    status = STATUS_SUCCESS;

CleanUp:
    if (!NT_SUCCESS(status))
    {
        /* Destroy TP on failure. */
        TpUninit(ThreadPool);
    }
    return status;
}

NTSTATUS
TpEnqueueWorkItem(
    _Inout_ MY_THREAD_POOL* ThreadPool,
    _In_ LPTHREAD_START_ROUTINE WorkRoutine,
    _In_opt_ PVOID Context
)
{
    /* Allocate space for the work item. Will be freed when item is processed. */
    MY_WORK_ITEM* item = (MY_WORK_ITEM*)malloc(sizeof(MY_WORK_ITEM));
    if (NULL == item)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(item, sizeof(MY_WORK_ITEM));

    /* Assign fields. */
    item->Context = Context;
    item->WorkRoutine = WorkRoutine;

    /* todo Insert into threadpool. This must be done with lock taken. */
    AcquireSRWLockExclusive(&ThreadPool->QueueLock);
    ListInsertHead(&ThreadPool->Queue, &item->ListEntry);
    ReleaseSRWLockExclusive(&ThreadPool->QueueLock);

    /* todo Notify thread pool WorkScheduledEvent that a new item is available. */
    if (!SetEvent(ThreadPool->WorkScheduledEvent))
    {
        printf("SetEvent failed (%d)\n", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    /* All good. */
    return STATUS_SUCCESS;
}


//
// **********************************************************
// *                        Testing API                     *
// **********************************************************
//
typedef struct _MY_CONTEXT
{
    SRWLOCK ContextLock;
    UINT32 Number;
} MY_CONTEXT;

DWORD WINAPI
TestThreadPoolRoutine(
    _In_opt_ PVOID Context
)
{
    Sleep(2000);
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

typedef NTSTATUS(WINAPI* NQIP)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

VOID
PrintProcessNameAndID(
    DWORD processID
)
{
    TCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");
    PROCESS_BASIC_INFORMATION processInformation;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID);

    if (NULL != hProcess)
    {
        HMODULE hMod;
        DWORD cbNeeded;

        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded))
        {
            GetModuleBaseName(hProcess, hMod, szProcessName, sizeof(szProcessName) / sizeof(TCHAR));
        }

        //_tprintf(TEXT("%s  (PID: %u)\n"), szProcessName, processID);
        if (_tcscmp(szProcessName, TEXT("<unknown>")) != 0) {
            //printf("%s  (PID: %u)\n", szProcessName, processID);
            //_tprintf(TEXT("%s  (PID: %u)\n"), szProcessName, processID);

            if (!OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processID))
            {
                printf("OpenProcess failed (%d)\n", GetLastError());
                return;
            }

            HMODULE ntdll = LoadLibraryA("ntdll.dll");

            NQIP NtQueryInformationProcess = (NQIP)GetProcAddress(ntdll, "NtQueryInformationProcess");

            NtQueryInformationProcess(hProcess, ProcessBasicInformation, &processInformation, sizeof(processInformation), NULL);

            //printf("%p\n", processInformation.UniqueProcessId);

            PEB peb;
            if (!ReadProcessMemory(hProcess, processInformation.PebBaseAddress, &peb, sizeof(peb), NULL))
            {
                printf("ReadProcessMemory failed (%d)\n", GetLastError());
                return;
            }

            RTL_USER_PROCESS_PARAMETERS processParameters;
            if (!ReadProcessMemory(hProcess, peb.ProcessParameters, &processParameters, sizeof(processParameters), NULL))
            {
                printf("ReadProcessMemory failed (%d)\n", GetLastError());
                return;
            }

            PWSTR buffer = processParameters.CommandLine.Buffer;
            USHORT len = processParameters.CommandLine.Length;
            PWSTR bufferCopy = (PWSTR)malloc(len);

            if (!ReadProcessMemory(hProcess, buffer, bufferCopy, len, NULL))
            {
                printf("ReadProcessMemory failed (%d)\n", GetLastError());
                return;
            }

            printf("=========================================================================================\n");
            _tprintf(TEXT("Process name: %s  (PID: %u)\n"), szProcessName, processID);
            _tprintf(TEXT("Command line of process: %s\n"), bufferCopy);

            free(bufferCopy);
        }

        CloseHandle(hProcess);
    }
}

INT
ListAllRunningProcesses()
{
    DWORD aProcesses[1024], cbNeeded, cProcesses;
    unsigned int i;

    if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded))
    {
        return 1;
    }

    cProcesses = cbNeeded / sizeof(DWORD);

    for (i = 0; i < cProcesses; i++)
    {
        if (aProcesses[i] != 0)
        {
            PrintProcessNameAndID(aProcesses[i]);
        }
    }

    return 0;
}

int Error(const char* message) {
    printf("%s (error=%d)\n", message, GetLastError());
    return 1;
}

INT
DriverThreadPoolInit(int numberOfThreads)
{
    HANDLE hDevice = CreateFileA("\\\\?\\MyDriver",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        return Error("Failed to open device");
    }

    DWORD returned;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_DRIVER_INIT_TPOOL,
        &numberOfThreads, sizeof(numberOfThreads),
        nullptr, 0,
        &returned, nullptr
    );
    if (success)
    {
        printf("Threadpool init request successfully sent to the driver!\n");
        return 0;
    }
    else
    {
        return Error("!(Threadpool init request successfully sent to the driver!)");
    }
}

INT
DriverThreadPoolProcess()
{
    HANDLE hDevice = CreateFileA("\\\\?\\MyDriver",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        return Error("Failed to open device");
    }

    DWORD returned;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_DRIVER_PROCESS_TPOOL,
        NULL, 0,
        nullptr, 0,
        &returned, nullptr
    );
    if (success)
    {
        printf("Threadpool process request successfully sent to the driver!\n");
        return 0;
    }
    else
    {
        return Error("!(Threadpool process request successfully sent to the driver!)");
    }
}

INT
DriverThreadPoolTest()
{
    HANDLE hDevice = CreateFileA("\\\\?\\MyDriver",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        return Error("Failed to open device");
    }

    DWORD returned;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_DRIVER_TEST_TPOOL,
        NULL, 0,
        nullptr, 0,
        &returned, nullptr
    );
    if (success)
    {
        printf("Threadpool test request successfully sent to the driver!\n");
        return 0;
    }
    else
    {
        return Error("!(Threadpool test request successfully sent to the driver!)");
    }
}

INT
DriverThreadPoolUninit()
{
    HANDLE hDevice = CreateFileA("\\\\?\\MyDriver",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        return Error("Failed to open device");
    }

    DWORD returned;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_DRIVER_UNINIT_TPOOL,
        NULL, 0,
        nullptr, 0,
        &returned, nullptr
    );
    if (success)
    {
        printf("Threadpool uninit request successfully sent to the driver!\n");
        return 0;
    }
    else
    {
        return Error("!(Threadpool uninit request successfully sent to the driver!)");
    }
}

INT
DriverProtectProcess(int* pidsToProtect)
{
    HANDLE hDevice = CreateFileA("\\\\?\\MyDriver",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        return Error("Failed to open device");
    }

    DWORD returned;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_PROCESS_PROTECT_BY_PID,
        pidsToProtect, pidsToProtect[0] * sizeof(int),
        nullptr, 0,
        &returned, nullptr
    );
    if (success)
    {
        printf("Process protect by pid request successfully sent to the driver!\n");
        return 0;
    }
    else
    {
        return Error("!(Process protect by pid request successfully sent to the driver!)");
    }
}

INT
DriverUnprotectProcess(int* pidsToUnprotect)
{
    HANDLE hDevice = CreateFileA("\\\\?\\MyDriver",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        return Error("Failed to open device");
    }

    DWORD returned;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_PROCESS_UNPROTECT_BY_PID,
        pidsToUnprotect, pidsToUnprotect[0] * sizeof(int),
        nullptr, 0,
        &returned, nullptr
    );

    if (success)
    {
        printf("Process unprotect by pid request successfully sent to the driver!\n");
        return 0;
    }
    else
    {
        return Error("!(Process unprotect by pid request successfully sent to the driver!)");
    }
}

INT
DriverClearProtectProcess()
{
    HANDLE hDevice = CreateFileA("\\\\?\\MyDriver",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        return Error("Failed to open device");
    }

    DWORD returned;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_PROCESS_PROTECT_CLEAR,
        NULL, 0,
        nullptr, 0,
        &returned, nullptr
    );
    if (success)
    {
        printf("Process protect clear request successfully sent to the driver!\n");
        return 0;
    }
    else
    {
        return Error("!(Process protect clear request successfully sent to the driver!)");
    }
}

int parseArguments(char* userInput, int* arr)
{
    char* next_token = NULL;
    char* p = strtok_s(userInput, " ", &next_token);

    int nr = 0;
    while (p != NULL)
    {
        if (nr > 0)
            arr[nr] = atoi(p);

        p = strtok_s(NULL, " ", &next_token);

        nr += 1;

        if (nr > 5)
            break;
    }

    return nr;
}

int findPid(const WCHAR* procName)
{
    HANDLE hSnapshot;
    PROCESSENTRY32W pe;
    int pid = 0;
    BOOL hResult;

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    pe.dwSize = sizeof(PROCESSENTRY32W);
    hResult = Process32FirstW(hSnapshot, &pe);

    while (hResult)
    {
        if (wcscmp(procName, pe.szExeFile) == 0)
        {
            pid = pe.th32ProcessID;
            break;
        }

        hResult = Process32NextW(hSnapshot, &pe);
    }

    CloseHandle(hSnapshot);
    return pid;
}

typedef struct _REPLY_DATA
{
    WCHAR message[1024];
    ULONG messageLength;
} REPLY_DATA, * PREPLY_DATA;

typedef struct _MY_CUSTOM_MESSAGE
{
    FILTER_MESSAGE_HEADER headers;
    REPLY_DATA replyData;

} MY_CUSTOM_MESSAGE, * PMY_CUSTOM_MESSAGE;

NTSTATUS
SendCustomMessageToDriver(HANDLE port, const WCHAR* message)
{
    //__debugbreak();

    MY_CUSTOM_MESSAGE customMessage = { 0 };
	ULONG bytesReturned = 0;

    wcscpy_s(customMessage.replyData.message, sizeof(customMessage.replyData.message) / sizeof(WCHAR), message);
    customMessage.replyData.messageLength = wcslen(customMessage.replyData.message) * sizeof(WCHAR);

	printf("Sending message to driver: %S\n", customMessage.replyData.message);

    return FilterSendMessage(port, &customMessage, sizeof(MY_CUSTOM_MESSAGE), NULL, 0, &bytesReturned);
}

DWORD WINAPI GetWorker(PVOID ctx)
{
    HANDLE port = (HANDLE)ctx;

    while (true)
    {
        MY_CUSTOM_MESSAGE msg = { 0 };

        HRESULT hr = FilterGetMessage(
            port,
            &msg.headers,
            sizeof(msg),
            NULL
        );

        if (!SUCCEEDED(hr))
        {
            printf("Something went wrong\n");
            break;
        }
            
        printf("Message: %S\n", msg.replyData.message);
    }

    return 0;
}

int main() {
    HANDLE port;
    HRESULT hr;

	MY_CUSTOM_MESSAGE customMessage = { 0 };

    printf("Connecting to driver...\n");

    hr = FilterConnectCommunicationPort(L"\\MyFilterPort", 0, NULL, 0, NULL, &port);
    if (IS_ERROR(hr)) {
        printf("Connection failed. Error 0x%X\n", hr);
        return 1;
    }

    //__debugbreak();

    WCHAR userInput[1024] = { 0 };
    printf("Enter command: ");
	scanf_s("%ls", userInput, (unsigned)_countof(userInput));
    //printf("Sending image to driver\n");

	SendCustomMessageToDriver(port, userInput);

	//printf("Image sent to driver\n");

	MY_THREAD_POOL tp = { 0 };
	MY_CONTEXT ctx = { 0 };

    NTSTATUS status = TpInit(&tp, 5);
    if (!NT_SUCCESS(status))
    {
    }
    
    InitializeSRWLock(&ctx.ContextLock);
    ctx.Number = 0;

    //printf("Image sent to driver\n");

    for (int i = 0; i < 5; i++)
    {
        TpEnqueueWorkItem(&tp, GetWorker, port);
    }

    WaitForMultipleObjects(tp.NumberOfThreads, tp.ThreadHandles, TRUE, INFINITE);

    CloseHandle(port);
    return 0;
}