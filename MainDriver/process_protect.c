#include "process_protect.h"


GLOBALS globals;

BOOLEAN AddProcess(_In_ ULONG Pid);
BOOLEAN RemoveProcess(_In_ ULONG Pid);
BOOLEAN FindProcess(_In_ ULONG Pid);
OB_PREOP_CALLBACK_STATUS OnPreOpenProcess(PVOID Context, POB_PRE_OPERATION_INFORMATION Info);


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

NTSTATUS 
ProcessProtectDeviceControl(PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_SUCCESS;
	ULONG len = 0;

	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
		case IOCTL_PROCESS_PROTECT_BY_PID:
		{
			ULONG size = stack->Parameters.DeviceIoControl.InputBufferLength;
			if (size % sizeof(ULONG) != 0)
			{
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			PULONG data = (ULONG*)Irp->AssociatedIrp.SystemBuffer;

			KIRQL OldIrql;
			for (int i = 0; i < size / sizeof(ULONG); i++)
			{
				ULONG pid = data[i];
				if (pid == 0)
				{
					status = STATUS_INVALID_PARAMETER;
					break;
				}

				KeAcquireSpinLock(&globals.Lock, &OldIrql);
				if (FindProcess(pid))
				{
					KeReleaseSpinLock(&globals.Lock, OldIrql);
					continue;
				}
				else
				{
					KeReleaseSpinLock(&globals.Lock, OldIrql);
				}

				if (globals.PidsCount == MAXPIDS)
				{
					status = STATUS_TOO_MANY_CONTEXT_IDS;
					break;
				}

				KeAcquireSpinLock(&globals.Lock, &OldIrql);
				if (!AddProcess(pid))
				{
					status = STATUS_UNSUCCESSFUL;
					KeReleaseSpinLock(&globals.Lock, OldIrql);
					break;
				}
				else
				{
					KeReleaseSpinLock(&globals.Lock, OldIrql);
				}

				KeAcquireSpinLock(&globals.Lock, &OldIrql);
				len += sizeof(ULONG);
				KeReleaseSpinLock(&globals.Lock, OldIrql);
			}

			break;
		}

		case IOCTL_PROCESS_UNPROTECT_BY_PID:
		{
			ULONG size = stack->Parameters.DeviceIoControl.InputBufferLength;
			if (size % sizeof(ULONG) != 0)
			{
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			PULONG data = (ULONG*)Irp->AssociatedIrp.SystemBuffer;

			KIRQL OldIrql;
			for (int i = 0; i < size / sizeof(ULONG); i++)
			{
				ULONG pid = data[i];
				if (pid == 0)
				{
					status = STATUS_INVALID_PARAMETER;
					break;
				}

				KeAcquireSpinLock(&globals.Lock, &OldIrql);
				if (!RemoveProcess(pid))
				{
					KeReleaseSpinLock(&globals.Lock, OldIrql);
					continue;
				}
				else
				{
					KeReleaseSpinLock(&globals.Lock, OldIrql);
				}

				KeAcquireSpinLock(&globals.Lock, &OldIrql);
				len += sizeof(ULONG);
				KeReleaseSpinLock(&globals.Lock, OldIrql);

				if (globals.PidsCount == 0)
				{
					break;
				}
			}

			break;
		}
	
		case IOCTL_PROCESS_PROTECT_CLEAR:
		{
			KIRQL OldIrql;
			KeAcquireSpinLock(&globals.Lock, &OldIrql);
			memset(&globals.Pids, 0, sizeof(globals.Pids));
			KeReleaseSpinLock(&globals.Lock, OldIrql);

			break;
		}

		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = len;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
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