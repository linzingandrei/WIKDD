#include <ntddk.h>

#include "MyDriver.h"

void MyDriverUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS MyCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS MyDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS MyCompletionRoutine(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_ PVOID Context);

typedef struct {
	PDEVICE_OBJECT LowerDeviceObject;
	PFILE_OBJECT FileObject;
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

	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
		case IOCTL_MY_DRIVER_READ: {
			DbgPrintEx(0, 0, "First IOCTL was called\n");
			break;
		}

		case IOCTL_MY_DRIVER_WRITE: {
			DbgPrintEx(0, 0, "The second IOCTL was called\n");
			break;
		}

		case IOCTL_MY_DRIVER_FORWARD: {
			DbgPrintEx(0, 0, "The forward IOCTL was called\n");

			UNICODE_STRING dev2Name = RTL_CONSTANT_STRING(L"\\Device\\MyDriver2");

			PDEVICE_OBJECT LowerDeviceObject = NULL;

			PDEVICE_EXTENSION devExt;
			devExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
			devExt->LowerDeviceObject = LowerDeviceObject;

			status = IoGetDeviceObjectPointer(
				&dev2Name,
				FILE_READ_DATA,
				&devExt->FileObject,
				&devExt->LowerDeviceObject
			);

			if (!NT_SUCCESS(status))
			{
				DbgPrintEx(0, 0, "IoGetDeviceObjectPointer failed with status: (0x%08X)\n", status);
				return status;
			}

			if (devExt->LowerDeviceObject)
			{
				ObDereferenceObject(devExt->FileObject);
			}

			IoCopyCurrentIrpStackLocationToNext(Irp);

			IoSetCompletionRoutine(Irp, MyCompletionRoutine, NULL, TRUE, TRUE, TRUE);

			return IoCallDriver(devExt->LowerDeviceObject, Irp);
			
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