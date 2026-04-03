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

void InitializeProcessProtectRoutine();