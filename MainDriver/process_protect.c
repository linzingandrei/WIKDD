#include "process_protect.h"


void InitializeProcessProtectRoutine()
{
	KeInitializeSpinLock(&globals.Lock);

	OB_OPERATION_REGISTRATION operations[] = {
		{
			PsProcessType,
			OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE,
			OnPreOpenProcess, NULL
		}
	};

	OB_CALLBACK_REGISTRATION reg = {
		OB_FLT_REGISTRATION_VERSION,
		1,
		RTL_CONSTANT_STRING(L"30000"),
		NULL,
		operations
	};

	NTSTATUS status = STATUS_SUCCESS;

	do {
		status = ObRegisterCallbacks(&reg, &globals.RegHandle);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(0, 0, "Failed to register callback (0x%08X)\n", status);
			break;
		}
	} while (FALSE);
}

OB_PREOP_CALLBACK_STATUS
OnPreOpenProcess(PVOID Context, POB_PRE_OPERATION_INFORMATION Info)
{
	UNREFERENCED_PARAMETER(Context);

	if (Info->KernelHandle)
	{
		return OB_PREOP_SUCCESS;
	}

	PEPROCESS process = (PEPROCESS)Info->Object;
	ULONG pid = HandleToULong(PsGetProcessId(process));

	KIRQL OldIrql;
	KeAcquireSpinLock(&globals.Lock, &OldIrql);
	if (FindProcess(pid))
	{
		KeReleaseSpinLock(&globals.Lock, OldIrql);
		Info->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_TERMINATE;
	}
	else
	{
		KeReleaseSpinLock(&globals.Lock, OldIrql);
	}

	return OB_PREOP_SUCCESS;
}

BOOLEAN AddProcess(_In_ ULONG Pid)
{
	for (int i = 0; i < MAXPIDS; i++)
	{
		if (globals.Pids[i] == 0)
		{
			globals.Pids[i] = Pid;
			globals.PidsCount += 1;
			return TRUE;
		}
	}
	return FALSE;
}

BOOLEAN RemoveProcess(_In_ ULONG Pid)
{
	for (int i = 0; i < MAXPIDS; i++)
	{
		if (globals.Pids[i] == Pid)
		{
			globals.Pids[i] = 0;
			globals.PidsCount -= 1;
			return TRUE;
		}
	}
	return FALSE;
}

BOOLEAN FindProcess(_In_ ULONG Pid)
{
	for (int i = 0; i < MAXPIDS; i++)
	{
		if (globals.Pids[i] == Pid)
		{
			return TRUE;
		}
	}
	return FALSE;
}