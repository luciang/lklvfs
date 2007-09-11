/**
* all device control operations should be here
**/

#include <lklvfs.h>

NTSTATUS LklPrepareToUnload(PDEVICE_OBJECT device,PIRP irp);

NTSTATUS LklDeviceControl(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status=STATUS_SUCCESS;
	BOOLEAN top_level;
	ULONG ioctl = 0;
	PIO_STACK_LOCATION stack_location = NULL;
	PIO_STACK_LOCATION next_stack_location = NULL;

	ASSERT(device);
	ASSERT(irp);

	DbgPrint("Device Control");
	FsRtlEnterFileSystem();
	top_level = LklIsIrpTopLevel(irp);
	__try {
		stack_location = IoGetCurrentIrpStackLocation(irp);
		ASSERT(stack_location);

		ioctl = stack_location->Parameters.DeviceIoControl.IoControlCode;

		if (ioctl == IOCTL_PREPARE_TO_UNLOAD) {
			status = LklPrepareToUnload(device,irp);
			__leave;
		}
		//for now we support only prepare_unload
		status = STATUS_SUCCESS;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
			status = GetExceptionCode();
			DbgPrint("Exception %x in device control function", status);
		}
	LklCompleteRequest(irp, status);
	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;
}

//
//	IOCTL_PREPARE_TO_UNLOAD
//
NTSTATUS LklPrepareToUnload(PDEVICE_OBJECT device,PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	BOOLEAN acq_resource = FALSE;

	DbgPrint("PREPARE TO UNLOAD CALLED");

	__try
	{

		CHECK_OUT(device != lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);

		ExAcquireResourceExclusiveLite(&lklfsd.global_resource, TRUE);
		acq_resource = TRUE;

		CHECK_OUT(FLAG_ON(lklfsd.flags, VFS_UNLOAD_PENDING), STATUS_ACCESS_DENIED);
		CHECK_OUT(!IsListEmpty(&lklfsd.vcb_list), STATUS_ACCESS_DENIED);

		IoUnregisterFileSystem(lklfsd.device);
		IoDeleteDevice(lklfsd.device);

		SET_FLAG(lklfsd.flags, VFS_UNLOAD_PENDING);
try_exit:
		;
	}
	__finally
	{
		if (acq_resource)
			RELEASE(&lklfsd.global_resource);
	}
	return status;
}

//
//	mark the irp pending if it was sent to the underlying device --we'll need this in the future
//
NTSTATUS LklIoctlCompletion(PDEVICE_OBJECT device, PIRP irp, PVOID context)
{
	if (irp->PendingReturned)
		IoMarkIrpPending(irp);
	return STATUS_SUCCESS;
}
