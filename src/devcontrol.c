/**
* all device control operations should be here
* Put all TODOs here: none
*
**/

#include <lklvfs.h>


NTSTATUS LklIoctlCompletion(PDEVICE_OBJECT device, PIRP irp, PVOID context);
NTSTATUS LklPrepareToUnload(PDEVICE_OBJECT device,PIRP irp);
VOID DDKAPI DriverUnload(PDRIVER_OBJECT driver);

//
//	IRP_MJ_DEVICE_CONTROL - is synchronous
//
NTSTATUS DDKAPI VfsDeviceControl(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status=STATUS_SUCCESS;
	BOOLEAN top_level;
	ULONG ioctl = 0;
	PIO_STACK_LOCATION stack_location = NULL;
	PIO_STACK_LOCATION next_stack_location = NULL;
	BOOLEAN complete_request = FALSE;
	PLKLVCB vcb = NULL;
	PLKLFCB fcb = NULL;
	PFILE_OBJECT file = NULL;
	PDEVICE_OBJECT targetDevice = NULL;

	ASSERT(device);
	ASSERT(irp);
	top_level = LklIsIrpTopLevel(irp);
	FsRtlEnterFileSystem();

	stack_location = IoGetCurrentIrpStackLocation(irp);
	ASSERT(stack_location);

	ioctl = stack_location->Parameters.DeviceIoControl.IoControlCode;

	if (ioctl == IOCTL_PREPARE_TO_UNLOAD) {
		complete_request = TRUE;
		DbgPrint("Prepare to unload");
		status = LklPrepareToUnload(device,irp);
		TRY_RETURN(status);
	}

	file = stack_location->FileObject;
	ASSERT(file);
	fcb = (PLKLFCB) file->FsContext;
	CHECK_OUT(fcb == NULL, STATUS_INVALID_PARAMETER);

	if (fcb->id.type == VCB) {
		vcb = (PLKLVCB) fcb;
	} else {
		vcb = fcb->vcb;
	}
	CHECK_OUT(vcb == NULL, STATUS_INVALID_PARAMETER);

	targetDevice = vcb->target_device;

	// Pass on the IOCTL to the driver below
	complete_request = FALSE;
	next_stack_location = IoGetNextIrpStackLocation(irp);
	*next_stack_location = *stack_location;
	IoSetCompletionRoutine(irp, LklIoctlCompletion, NULL, TRUE, TRUE, TRUE);
	status = IoCallDriver(targetDevice, irp);

try_exit:
	
	if(complete_request)
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

	CHECK_OUT(device != lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);

	ExAcquireResourceExclusiveLite(&lklfsd.global_resource, TRUE);
	acq_resource = TRUE;

	CHECK_OUT(FLAG_ON(lklfsd.flags, VFS_UNLOAD_PENDING), STATUS_ACCESS_DENIED);
	CHECK_OUT(!IsListEmpty(&lklfsd.vcb_list), STATUS_ACCESS_DENIED);
    	DbgPrint("Unloading LklVfs");
	IoUnregisterFileSystem(lklfsd.device);

	//unload_linux_kernel();
    
	IoDeleteDevice(lklfsd.device);
     
	SET_FLAG(lklfsd.flags, VFS_UNLOAD_PENDING);
        lklfsd.driver->DriverUnload = DriverUnload;

try_exit:

	if (acq_resource)
		RELEASE(&lklfsd.global_resource);

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
