#include "threadpool.h"
#include "threadpool_test.h"


#include <ntddk.h>

#include "main.h"

void MyDriverUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS MyCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS MyDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS MyCompletionRoutine(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_ PVOID Context);

typedef struct {
	PDEVICE_OBJECT LowerDeviceObject;
	PFILE_OBJECT FileObject;

	MY_THREAD_POOL tp;
	MY_CONTEXT ctx;

	int numberOfThreadPools;
}*PDEVICE_EXTENSION, DEVICE_EXTENSION;

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	__debugbreak();

	DriverObject->MajorFunction[IRP_MJ_CREATE] = MyCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = MyCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = MyDeviceControl;

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\MyDriver");

	PDEVICE_OBJECT DeviceObject;
	NTSTATUS status = IoCreateDevice(
		DriverObject,
		sizeof(DEVICE_EXTENSION),
		&devName,
		FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&DeviceObject
	);

	if (!NT_SUCCESS(status))
	{
		DbgPrintEx(0, 0, "Failed to create device object (0x%08X)\n", status);
		return status;
	}

	DeviceObject->StackSize = 2;

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\MyDriver");
	status = IoCreateSymbolicLink(&symLink, &devName);

	if (!NT_SUCCESS(status)) {
		DbgPrintEx(0, 0, "Failed to create symbolic link (0x%08X)\n", status);
		IoDeleteDevice(DeviceObject);
		return status;
	}

	DriverObject->DriverUnload = MyDriverUnload;
	return STATUS_SUCCESS;
}

void MyDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\MyDriver");

	IoDeleteSymbolicLink(&symLink);

	IoDeleteDevice(DriverObject->DeviceObject);
}

_Use_decl_annotations_
NTSTATUS MyCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS MyCompletionRoutine(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_ PVOID Context)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);
	UNREFERENCED_PARAMETER(Context);

	DbgPrintEx(0, 0, "Irp status: %d", Irp->IoStatus.Status);

	return STATUS_CONTINUE_COMPLETION;
}

_Use_decl_annotations_
NTSTATUS MyDeviceControl(PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	__debugbreak();

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_SUCCESS;

	PDEVICE_EXTENSION devExt = (PDEVICE_EXTENSION)(DeviceObject->DeviceExtension);

	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_DRIVER_INIT_TPOOL: {
		DbgPrintEx(0, 0, "The initialize threadpool IOCTL was called\n");

		RtlZeroMemory(&devExt->tp, sizeof(devExt->tp));
		RtlZeroMemory(&devExt->ctx, sizeof(devExt->ctx));

		devExt->numberOfThreadPools = *(int*)Irp->AssociatedIrp.SystemBuffer;

		status = TpInit(&devExt->tp, devExt->numberOfThreadPools);
		if (!NT_SUCCESS(status))
		{
			goto cleanup;
		}

		DbgPrintEx(0, 0, "Status = 0x%X\n", status);

		KeInitializeSpinLock(&devExt->ctx.ContextLock);
		devExt->ctx.Number = 0;

		//DbgPrintEx(0, 0, "IOCTL = 0x%X\n", stack->Parameters.DeviceIoControl.IoControlCode);

		break;
	}

	case IOCTL_DRIVER_PROCESS_TPOOL: {
		DbgPrintEx(0, 0, "The process threadpool IOCTL was called\n");

		for (int i = 0; i < 10; ++i)
		{
			DbgPrintEx(0, 0, "Hi %d!\n", devExt->ctx.Number);

			int nr = 1000000;
			while (nr > 0)
			{
				nr -= 1;
			}

			status = TpEnqueueWorkItem(&devExt->tp, SimpleTPProcess, &devExt->ctx);
			if (!NT_SUCCESS(status))
			{
				goto cleanup;
			}

			//DbgPrintEx(0, 0, "Hi %d!\n", devExt->ctx.Number);
		}
		DbgPrintEx(0, 0, "Hi %d!\n", devExt->ctx.Number);

		break;
	}

	case IOCTL_DRIVER_UNINIT_TPOOL: {
		DbgPrintEx(0, 0, "The uninitialize threadpool IOCTL was called\n");

		cleanup:
			TpUninit(&devExt->tp);
			DbgPrintEx(0, 0, "Status: %d", status);

		/* If everything went well, this should output 100000000. */
		DbgPrintEx(0, 0, "Final number value = %d \r\n", devExt->ctx.Number);

		break;
	}

	case IOCTL_DRIVER_TEST_TPOOL: {
		DbgPrintEx(0, 0, "The test threadpool IOCTL was called\n");

		if (devExt->ctx.Number != 0)
		{
			TpUninit(&devExt->tp);
			devExt->ctx.Number = 0;
		}

		status = TpInit(&devExt->tp, 5);
		if (!NT_SUCCESS(status))
		{
			goto cleanup;
		}

		for (int i = 0; i < 100000; ++i)
		{
			status = TpEnqueueWorkItem(&devExt->tp, TestThreadPoolRoutine, &devExt->ctx);
			if (!NT_SUCCESS(status))
			{
				goto cleanup;
			}

			if (i % 50000 == 0)
			{
				DbgPrintEx(0, 0, "Value: %d\n", devExt->ctx.Number);
			}
		}

		TpUninit(&devExt->tp);

		DbgPrintEx(0, 0, "Value: %d\n", devExt->ctx.Number);

		if (devExt->ctx.Number != 100000000) {
			DbgPrintEx(0, 0, "Result is not as expected!\n");
		}

		break;
	}

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}