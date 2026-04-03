#pragma once

#include "process_protect_common.h"
#include "wdm.h"
#include "ntddk.h"

#define PROCESS_TERMINATE 1
#define MAXPIDS 256

typedef struct _GLOBALS
{
	int PidsCount;
	ULONG Pids[MAXPIDS];
	KSPIN_LOCK Lock;
	PVOID RegHandle;
}GLOBALS;

GLOBALS globals;

void InitializeProcessProtectRoutine();
BOOLEAN AddProcess(_In_ ULONG Pid);
BOOLEAN RemoveProcess(_In_ ULONG Pid);
BOOLEAN FindProcess(_In_ ULONG Pid);
OB_PREOP_CALLBACK_STATUS OnPreOpenProcess(PVOID Context, POB_PRE_OPERATION_INFORMATION Info);