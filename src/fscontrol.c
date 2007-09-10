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
	// try a linux mount -- maybe get the sb
	//run_linux_kernel();
	// if this succeeds...
	status = IoCreateDevice(lklfsd.driver, QUAD_ALIGN(sizeof(LKLVCB)), NULL,
			FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &volume_device);
	if (!NT_SUCCESS(status))
		return status;
	if (dev->AlignmentRequirement > volume_device->AlignmentRequirement)
			volume_device->AlignmentRequirement = dev->AlignmentRequirement;
	CLEAR_FLAG(volume_device->Flags, DO_DEVICE_INITIALIZING);
	volume_device->StackSize = (CCHAR)(dev->StackSize+1);

	vpb->DeviceObject = volume_device;
	// complete vpb fields from sb fields --TODO--

	LklCreateVcb(volume_device,dev,vpb,&AllocationSize);

	return STATUS_SUCCESS;
}

//
//	IRP_MN_MOUNT_VOLUME
//
NTSTATUS LklMountVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	NTSTATUS status = STATUS_UNRECOGNIZED_VOLUME;
	PDEVICE_OBJECT target_dev;
	PDEVICE_OBJECT target_device=NULL;
	PFILE_OBJECT target_device_file=NULL;
	PVPB vpb;
	UNICODE_STRING target_dev_name;

	__try
	{
		// try to mount only our device ( TODO: fix this)
		RtlInitUnicodeString(&target_dev_name, LKL_DEVICE);
		status=IoGetDeviceObjectPointer(&target_dev_name, FILE_READ_ATTRIBUTES , &target_device_file, &target_device);
		CHECK_OUT(!NT_SUCCESS(status), STATUS_UNRECOGNIZED_VOLUME);

		vpb=stack_location->Parameters.MountVolume.Vpb;
		target_dev=stack_location->Parameters.MountVolume.DeviceObject;
		// never verify the volume - ??
		if(target_dev == target_device){
			if (FLAG_ON(vpb->RealDevice->Flags, DO_VERIFY_VOLUME)) {
				CLEAR_FLAG(vpb->RealDevice->Flags, DO_VERIFY_VOLUME);
			lklfsd.physical_device=vpb->RealDevice;
			}
			status=LklMount(target_dev, vpb);
		}
		else
			status=STATUS_UNRECOGNIZED_VOLUME;

	try_exit:
		;
	}
	__finally
	{
		if(target_device_file)
			ObDereferenceObject(&target_device_file);
	}

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
	NTSTATUS status = STATUS_SUCCESS;
	//TODO
	DbgPrint("Lock Volume");
	return status;
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
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	//TODO
	DbgPrint("Unlock volume");
	return status;
}

NTSTATUS LklUmount(IN PDEVICE_OBJECT dev,IN PFILE_OBJECT file)
{
	NTSTATUS status=STATUS_UNSUCCESSFUL;
	PLKLVCB vcb=NULL;
	BOOLEAN notified = FALSE;
	BOOLEAN vcb_acquired = FALSE;

	ASSERT(file);
	ASSERT(dev);

	vcb=(PLKLVCB) dev->DeviceExtension;
	ASSERT(vcb);
	DbgPrint("Volume beeing DISMOUNTED");

	FsRtlNotifyVolumeEvent(file, FSRTL_VOLUME_DISMOUNT);
	notified = TRUE;
	ExAcquireResourceExclusiveLite(&vcb->vcb_resource, TRUE);
	vcb_acquired = TRUE;
	status=STATUS_SUCCESS;
	//TODO unmount volume  ( linux way )
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
	PDEVICE_OBJECT target_device=NULL;
	PFILE_OBJECT target_device_file=NULL;
	UNICODE_STRING target_dev_name;

	DbgPrint("Unmount volume");
	__try {
		device = stack_location->DeviceObject;

		CHECK_OUT(device == lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);

		file_obj = stack_location->FileObject;
		RtlInitUnicodeString(&target_dev_name, LKL_DEVICE);
		status=IoGetDeviceObjectPointer(&target_dev_name, FILE_READ_ATTRIBUTES , &target_device_file, &target_device);
		ASSERT(NT_SUCCESS(status));
		if(target_device==device)
			status = LklUmount(device, file_obj);
		else
			status = STATUS_UNSUCCESSFUL;
		ObDereferenceObject(&target_device_file);

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
