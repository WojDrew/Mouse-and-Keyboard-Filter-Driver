#include "ntddk.h"
#include "ntddmou.h"
#include <stdlib.h>

#define KEY_DOWN 0
#define KEY_UP 1
#define KEY_SPECIAL_DOWN 2
#define KEY_SPECIAL_UP 3


int x, y;

typedef struct {
	PDEVICE_OBJECT LowerDevice;
} DEVICE_EXTENSION, * PDEVICE_EXTENSION;

PDEVICE_OBJECT myKbdDevice = NULL;

PDEVICE_OBJECT myMouseDevice = NULL;

BOOLEAN send = FALSE;

BOOLEAN end = FALSE;

BOOLEAN eDown = FALSE;

BOOLEAN eUp = TRUE;

int speed = 5;

typedef struct _KEYBOARD_INPUT_DATA {
	USHORT UnitId;
	USHORT MakeCode;
	USHORT Flags;
	USHORT Reserved;
	ULONG  ExtraInformation;
} KEYBOARD_INPUT_DATA, * PKEYBOARD_INPUT_DATA;


ULONG pendingKey = 0;

VOID  Unload(
	IN PDRIVER_OBJECT DriverObject
)
{
	end = TRUE;
	LARGE_INTEGER interval = { 0 };
	interval.QuadPart = -10 * 1000 * 1000;
	IoDetachDevice(((PDEVICE_EXTENSION)myKbdDevice->DeviceExtension)->LowerDevice);
	IoDetachDevice(((PDEVICE_EXTENSION)myMouseDevice->DeviceExtension)->LowerDevice);
	while (pendingKey) {
		KeDelayExecutionThread(KernelMode,FALSE,&interval);
	}
	IoDeleteDevice(myKbdDevice);
	IoDeleteDevice(myMouseDevice);
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "driver unload\r\n");
}

NTSTATUS DispatchPass(
	PDEVICE_OBJECT DeviceObject, 
	PIRP Irp
)
{
	
	IoCopyCurrentIrpStackLocationToNext(Irp);
	return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->LowerDevice,Irp);
}

NTSTATUS ReadComplete(
	PDEVICE_OBJECT DeviceObject,
	PIRP Irp,
	PVOID Context
)
{
	PKEYBOARD_INPUT_DATA Keys = (PKEYBOARD_INPUT_DATA)Irp->AssociatedIrp.SystemBuffer;
	int structnum = Irp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA);

	if (Irp->IoStatus.Status == STATUS_SUCCESS)
	{
		x = 0;
		y = 0;
		BOOLEAN flag = FALSE;
		//TODO
		for (int i = 0; i < structnum; i++) {
			if (Keys[i].Flags == KEY_SPECIAL_DOWN && Keys[i].MakeCode == 0x48) {
				flag = TRUE;
				y = -speed;
				DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "up arrow was pressed\r\n");
			}
			else if (Keys[i].Flags == KEY_SPECIAL_DOWN && Keys[i].MakeCode == 0x4B) {
				flag = TRUE;
				x = -speed;
				DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "left arrow was pressed\r\n");
			}
			else if (Keys[i].Flags == KEY_SPECIAL_DOWN && Keys[i].MakeCode == 0x4D) {
				flag = TRUE;
				x = speed;
				DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "right arrow was pressed\r\n");
			}
			else if (Keys[i].Flags == KEY_SPECIAL_DOWN && Keys[i].MakeCode == 0x50) {
				flag = TRUE;
				y = speed;
				DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "down arrow was pressed\r\n");
			}
			else if (Keys[i].Flags == KEY_DOWN && Keys[i].MakeCode == 0x12) {
				flag = TRUE;
				eDown = TRUE;
				DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "e is down\r\n");
			}
			else if (Keys[i].Flags == KEY_UP && Keys[i].MakeCode == 0x12) {
				flag = TRUE;
				eUp = TRUE;
				DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "e is up\r\n");
			}
		}
		if (flag)
			send = TRUE;
	}

	if (Irp->PendingReturned)
	{
		IoMarkIrpPending(Irp);
	}
	pendingKey--;
	return Irp->IoStatus.Status;
	
}

NTSTATUS DispatchRead(
	PDEVICE_OBJECT DeviceObject,
	PIRP Irp
)
{
	if (DeviceObject == myKbdDevice) {
		IoCopyCurrentIrpStackLocationToNext(Irp);

		IoSetCompletionRoutine(Irp, ReadComplete, NULL, TRUE, TRUE, TRUE);
		pendingKey++;
		return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->LowerDevice, Irp);
	}
	else 
	{
		Irp->IoStatus.Status = STATUS_SUCCESS;
		Irp->IoStatus.Information = sizeof(MOUSE_INPUT_DATA);

		LARGE_INTEGER interval = { 0 };
		interval.QuadPart = -10+100;
		while (send == FALSE) {
			KeDelayExecutionThread(KernelMode, FALSE, &interval);
			if (end)
				return STATUS_SUCCESS;
		}
		send = FALSE;
 
		PMOUSE_INPUT_DATA mouse = ExAllocatePoolWithTag(PagedPool, sizeof(MOUSE_INPUT_DATA), 0x24);
		mouse->UnitId = 0;
		mouse->Flags = MOUSE_MOVE_RELATIVE;
		mouse->LastX = x;
		mouse->LastY = y;

		if (eDown) {
			mouse->ButtonFlags = MOUSE_LEFT_BUTTON_DOWN;
			eUp = FALSE;
		}
		else if(eUp){
			mouse->ButtonFlags = MOUSE_LEFT_BUTTON_UP;
			eDown = FALSE;
		}

		Irp->AssociatedIrp.SystemBuffer = mouse;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}
}

NTSTATUS AttachKeyboardDevice(
	PDRIVER_OBJECT DriverObject
) 
{
	NTSTATUS status;
	UNICODE_STRING TargetDevice = RTL_CONSTANT_STRING(L"\\Device\\KeyboardClass0");
	status = IoCreateDevice(DriverObject,sizeof(DEVICE_EXTENSION),NULL,FILE_DEVICE_KEYBOARD,0,FALSE,&myKbdDevice);

	if (!NT_SUCCESS(status))
		return status;

	myKbdDevice->Flags |= DO_BUFFERED_IO;
	myKbdDevice->Flags &= ~DO_DEVICE_INITIALIZING;

	RtlZeroMemory(myKbdDevice->DeviceExtension, sizeof(DEVICE_EXTENSION));

	status = IoAttachDevice(myKbdDevice, &TargetDevice, &((PDEVICE_EXTENSION)myKbdDevice->DeviceExtension)->LowerDevice);

	if (!NT_SUCCESS(status))
	{
		IoDeleteDevice(myKbdDevice);
		return status;
	}
	return STATUS_SUCCESS;
}

NTSTATUS AttachMouseDevice(
	PDRIVER_OBJECT DriverObject
)
{
	NTSTATUS status;
	UNICODE_STRING TargetDevice = RTL_CONSTANT_STRING(L"\\Device\\PointerClass0");
	status = IoCreateDevice(DriverObject, sizeof(DEVICE_EXTENSION), NULL, FILE_DEVICE_MOUSE, 0, FALSE, &myMouseDevice);

	if (!NT_SUCCESS(status))
		return status;

	myMouseDevice->Flags |= DO_BUFFERED_IO;
	myMouseDevice->Flags &= ~DO_DEVICE_INITIALIZING;

	RtlZeroMemory(myMouseDevice->DeviceExtension, sizeof(DEVICE_EXTENSION));

	status = IoAttachDevice(myMouseDevice, &TargetDevice, &((PDEVICE_EXTENSION)myMouseDevice->DeviceExtension)->LowerDevice);

	if (!NT_SUCCESS(status))
	{
		IoDeleteDevice(myMouseDevice);
		return status;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath
)
{
	x = 400;
	y = 400;
	NTSTATUS status;
	DriverObject->DriverUnload = Unload;

	for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
		DriverObject->MajorFunction[i] = DispatchPass;
	}

	DriverObject->MajorFunction[IRP_MJ_READ] = DispatchRead;

	status = AttachKeyboardDevice(DriverObject);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "attaching keyboard device failed\r\n");
		return status;
	}
	else
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "attaching keyboard device succeeds\r\n");

	status = AttachMouseDevice(DriverObject);
	if (!NT_SUCCESS(status))
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "attaching mouse device failed\r\n");
	else
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "attaching mouse device succeeds\r\n");
	return status;

}