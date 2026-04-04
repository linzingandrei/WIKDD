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

/*
* From what I understand and seen, out of these 3:
* taskkill /im notepad.exe
* taskkill /f /im notepad.exe
* using task manager->right click->end task
* 
* Only taskkill /f /im notepad.exe doesn't close it. I also found out that in task manager->details->end tasak it still can't close.
* I believe taskkill /im notepad.exe and using task manager main windows and ending it in there gracefully closes it meaning that it deletes the gui first.
* It also can't protect against gui closes (from inside the app). As per Windows Kernel Programming book:
*      "In the case of notepad, even with protection, clicking the window close button or selecting
		File/Exit from the menu would terminate the process. This is because it’s being done
		internally by calling ExitProcess which does not involve any handles. This means the
		protection mechanism we devised here is essentially good for processes without user
		interface."
*/

void InitializeProcessProtectRoutine();
BOOLEAN AddProcess(_In_ ULONG Pid);
BOOLEAN RemoveProcess(_In_ ULONG Pid);
BOOLEAN FindProcess(_In_ ULONG Pid);
OB_PREOP_CALLBACK_STATUS OnPreOpenProcess(PVOID Context, POB_PRE_OPERATION_INFORMATION Info);