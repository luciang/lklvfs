/**
* file system control operations
**/

#include <lklvfs.h>

//
//	IRP_MJ_FILE_SYSTEM_CONTROL
//
NTSTATUS LklFileSystemControl(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stack_location = NULL;

	ASSERT(device);
	ASSERT(irp);

	FsRtlEnterFileSystem();

	stack_location = IoGetCurrentIrpStackLocation(irp);

	switch (stack_location->MinorFunction) {
	case IRP_MN_MOUNT_VOLUME:
		status = LklMountVolume(irp, stack_location);
		LklCompleteRequest(irp, status);
		break;
	case IRP_MN_USER_FS_REQUEST:
		status = LklUserFileSystemRequest(irp, stack_location);
		LklCompleteRequest(irp, status);
		break;
	case IRP_MN_VERIFY_VOLUME:
		status = LklVerifyVolume(irp, stack_location);
		LklCompleteRequest(irp, status);
		break;
	case IRP_MN_LOAD_FILE_SYSTEM:
	default:
		LklCompleteRequest(irp, STATUS_INVALID_DEVICE_REQUEST);
		break;
	}

	FsRtlExitFileSystem();

	return status;
}

NTSTATUS LklMount(IN PDEVICE_OBJECT dev,IN PVPB vpb)
{
	NTSTATUS status;
	PDEVICE_OBJECT volume_device=NULL;
	LARGE_INTEGER AllocationSize;

	DbgPrint("Mount volume");
	// fix this - for now we allow only one mounted fs at a time
	__try{
		CHECK_OUT(dev == lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);
		CHECK_OUT((lklfsd.physical_device!=NULL), STATUS_UNRECOGNIZED_VOLUME);

		lklfsd.physical_device = dev;
		// try a linux mount -- maybe get the sb
		//status = run_linux_kernel(); // if this fails, then we fail to mount the volume
		// CHECK_OUT(!NT_SUCCESS(status), STATUS_UNRECOGNIZED_VOLUME);
		// if this succeeds...
		status = IoCreateDevice(lklfsd.driver, QUAD_ALIGN(sizeof(LKLVCB)), NULL,
				FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &volume_device);
		CHECK_OUT(!NT_SUCCESS(status), status);
		if (dev->AlignmentRequirement > volume_device->AlignmentRequirement)
				volume_device->AlignmentRequirement = dev->AlignmentRequirement;
		CLEAR_FLAG(volume_device->Flags, DO_DEVICE_INITIALIZING);
		volume_device->StackSize = (CCHAR)(dev->StackSize+1);

		vpb->DeviceObject = volume_device;
		// complete vpb fields from sb fields --TODO--

		LklCreateVcb(volume_device,dev,vpb,&AllocationSize);
try_exit:
	;
	}
	__finally{
		//TODO -- undo what we've done if we are unsuccesfull
	}

	return STATUS_SUCCESS;
}

//
//	IRP_MN_MOUNT_VOLUME
//
NTSTATUS LklMountVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	NTSTATUS status = STATUS_UNRECOGNIZED_VOLUME;
	PDEVICE_OBJECT target_dev;
	PVPB vpb;

	vpb=stack_location->Parameters.MountVolume.Vpb;
	target_dev=stack_location->Parameters.MountVolume.DeviceObject;
	status=LklMount(target_dev, vpb);

	return status;
}

//
//	IRP_MN_USER_REQUEST
//
NTSTATUS LklUserFileSystemRequest(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
	ULONG fs_ctrl_code = 0;
	PIO_STACK_LOCATION stack_location_ex = stack_location;

	fs_ctrl_code = stack_location_ex->Parameters.FileSystemControl.FsControlCode;
	switch (fs_ctrl_code) {
	case FSCTL_LOCK_VOLUME:
		status = LklLockVolume(irp, stack_location);
		break;
	case FSCTL_UNLOCK_VOLUME:
		status = LklUnlockVolume(irp, stack_location);
		break;
	case FSCTL_DISMOUNT_VOLUME:
		status = LklDismountVolume(irp, stack_location);
		break;
	case FSCTL_IS_VOLUME_MOUNTED:
		status = LklIsVolumeMounted(irp, stack_location);
		break;
	default:
		;
	}
	return status;
}

//
//	FSCTL_LOCK_VOLUME
//
NTSTATUS LklLockVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PDEVICE_OBJECT device = NULL;
	PLKLVCB vcb;
	BOOLEAN notified = FALSE;
	BOOLEAN resource_acquired = FALSE;
	PFILE_OBJECT file_obj = NULL;

	__try {
		// request to fs device not permited
		device = stack_location->DeviceObject;
		CHECK_OUT(device == lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);

		// get vcb
		vcb = (PLKLVCB)device->DeviceExtension;
		ASSERT(vcb);

		// file object - should be the file object for a volume open, even so we accept it for any open file
		file_obj = stack_location->FileObject;
		ASSERT(file_obj);

		// notify volume locked
		FsRtlNotifyVolumeEvent(file_obj, FSRTL_VOLUME_LOCK);
		notified = TRUE;

		// acquire vcb lock
		ExAcquireResourceSharedLite(&vcb->vcb_resource, TRUE);
		resource_acquired = TRUE;

		// check lock flag
		if (FLAG_ON(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED)) {
			LklVfsReportError("Volume already locked");
			TRY_RETURN(STATUS_ACCESS_DENIED);
		}

		// abort if open files still exist
		if (vcb->open_count) {
			LklVfsReportError("Open files still exist");
			TRY_RETURN(STATUS_ACCESS_DENIED);
		}

		// release lock
		RELEASE(&vcb->vcb_resource);
		resource_acquired = FALSE;

		// purge volume
		LklPurgeVolume(vcb, TRUE);

		// if there are still open referneces we can't lock the volume
		if (vcb->reference_count > 1) {
			LklVfsReportError("Could not purge cached files");
			TRY_RETURN(STATUS_ACCESS_DENIED);
		}

		ExAcquireResourceExclusiveLite(&vcb->vcb_resource, TRUE);
		resource_acquired = TRUE;

		// set flag in both vcb and vpb structures
		SET_FLAG(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED);
		LklSetVpbFlag(vcb->vpb, VFS_VCB_FLAGS_VOLUME_LOCKED);
		DbgPrint("*** Volume LOCKED ***\n");
		status = STATUS_SUCCESS;
try_exit:
		;
	}
	__finally {
		if (resource_acquired)
			RELEASE(&vcb->vcb_resource);
		if (!NT_SUCCESS(status) && notified)
			FsRtlNotifyVolumeEvent(file_obj, FSRTL_VOLUME_LOCK_FAILED);
	}

	return status;
}

void LklPurgeFile(PLKLFCB fcb, BOOLEAN flush_before_purge)
{
	IO_STATUS_BLOCK iosb;

	ASSERT(fcb);
	DbgPrint("Purge files");
	//TODO
}

//
//	purges any files that are still referenced, most probably by the cache mgr
//
void LklPurgeVolume(PLKLVCB vcb, BOOLEAN flush_before_purge)
{
	//TODO
	DbgPrint("Purge Volume");
}



void LklSetVpbFlag(PVPB vpb,IN USHORT flag)
{
	KIRQL irql;
	IoAcquireVpbSpinLock(&irql);
	vpb->Flags |= flag;
	IoReleaseVpbSpinLock(irql);
}

void LklClearVpbFlag(PVPB vpb,IN USHORT flag)
{
	KIRQL irql;
	IoAcquireVpbSpinLock(&irql);
	vpb->Flags &= ~flag;
	IoReleaseVpbSpinLock(irql);
}

NTSTATUS LklUnlockVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	PDEVICE_OBJECT device = NULL;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PLKLVCB vcb = NULL;
	BOOLEAN vcb_acquired = FALSE;
	PFILE_OBJECT file_obj = NULL;

	__try {
		device = stack_location->DeviceObject;
		CHECK_OUT(device == lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);

		vcb = (PLKLVCB)device->DeviceExtension;
		ASSERT(vcb);

		file_obj = stack_location->FileObject;
		ASSERT(file_obj);

		ExAcquireResourceExclusiveLite(&vcb->vcb_resource, TRUE);
		vcb_acquired = TRUE;

		if (!FLAG_ON(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED)) {
			LklVfsReportError("Volume is NOT LOCKED");
			TRY_RETURN(STATUS_ACCESS_DENIED);
		}

		CLEAR_FLAG(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED);
		LklClearVpbFlag(vcb->vpb, VFS_VCB_FLAGS_VOLUME_LOCKED);
		DbgPrint("*** Volume UNLOCKED ***");

		status = STATUS_SUCCESS;
try_exit:
		;
	}
	__finally {
		if (vcb_acquired)
			RELEASE(&vcb->vcb_resource);
		FsRtlNotifyVolumeEvent(file_obj, FSRTL_VOLUME_UNLOCK);
	}
	return status;
}

NTSTATUS LklUmount(IN PDEVICE_OBJECT dev,IN PFILE_OBJECT file)
{
	NTSTATUS status=STATUS_UNSUCCESSFUL;
	PLKLVCB vcb=NULL;
	BOOLEAN notified = FALSE;
	BOOLEAN vcb_acquired = FALSE;

	vcb=(PLKLVCB) dev->DeviceExtension;
	ASSERT(vcb);
	DbgPrint("Volume beeing DISMOUNTED");

	FsRtlNotifyVolumeEvent(file, FSRTL_VOLUME_DISMOUNT);
	notified = TRUE;
	ExAcquireResourceExclusiveLite(&vcb->vcb_resource, TRUE);
	vcb_acquired = TRUE;
	status=STATUS_SUCCESS;
	//TODO unmount volume  ( linux way )
	// status=...
	if (!FLAG_ON(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED)) {
			LklVfsReportError("Volume is NOT LOCKED");
			return(STATUS_ACCESS_DENIED);
		}

	SET_FLAG(vcb->flags, VFS_VCB_FLAGS_BEING_DISMOUNTED);
	if (vcb_acquired)
			RELEASE(&vcb->vcb_resource);
	if (!NT_SUCCESS(status) && notified)
		FsRtlNotifyVolumeEvent(file, FSRTL_VOLUME_DISMOUNT_FAILED);
	return status;
}


NTSTATUS LklDismountVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	PDEVICE_OBJECT device = NULL;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PLKLVCB vcb = NULL;
	PFILE_OBJECT file_obj = NULL;

	DbgPrint("Unmount volume");

	__try {
		device = stack_location->DeviceObject;

		CHECK_OUT(device == lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);

		file_obj = stack_location->FileObject;
		status = LklUmount(device, file_obj);

try_exit:
		;
	}
	__finally
	{
	}
	return status;
}

NTSTATUS LklIsVolumeMounted(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	DbgPrint("Is volume mounted ?");
	return LklVerifyVolume(irp, stack_location);
}

NTSTATUS LklVerifyVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	PDEVICE_OBJECT device = NULL;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PLKLVCB vcb = NULL;
	BOOLEAN vcb_acquired = FALSE;

	DbgPrint("Verify volume");
	__try {
		device = stack_location->DeviceObject;
		CHECK_OUT(device == lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);

		vcb = (PLKLVCB) device->DeviceExtension;
		ASSERT(vcb);

		ExAcquireResourceExclusiveLite(&vcb->vcb_resource, TRUE);
		vcb_acquired = TRUE;

		CLEAR_FLAG(vcb->target_device->Flags, DO_VERIFY_VOLUME);

		status = STATUS_SUCCESS;
try_exit:
		;
	}
	__finally {
		if (vcb_acquired)
			RELEASE(&vcb->vcb_resource);
	}
	return status;
}
