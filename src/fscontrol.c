/**
* file system control operations
* TODOs:
* - to fix: for now we allow only one mounted fs at a time
* FIXME (BUG) when I try to mount the fs the second time, it gets me a BSOD
**/

#include <lklvfs.h>
//
//	IRP_MJ_FILE_SYSTEM_CONTROL
//
NTSTATUS DDKAPI VfsFileSystemControl(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stack_location = NULL;

	ASSERT(device);
	ASSERT(irp);

	FsRtlEnterFileSystem();

	stack_location = IoGetCurrentIrpStackLocation(irp);

	switch (stack_location->MinorFunction) {
	case IRP_MN_MOUNT_VOLUME:
		status = VfsMountVolume(irp, stack_location);
		LklCompleteRequest(irp, status);
		break;
	case IRP_MN_USER_FS_REQUEST:
		status = VfsUserFileSystemRequest(irp, stack_location);
		LklCompleteRequest(irp, status);
		break;
	case IRP_MN_VERIFY_VOLUME:
		status = VfsVerifyVolume(irp, stack_location);
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
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT volume_device=NULL;
	LARGE_INTEGER AllocationSize;
	ULONG ioctlSize;
    STATFS my_stat;
    ULONG rc;
    
	CHECK_OUT(dev == lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);
	//FIXME: we allow only one mounted volume
	CHECK_OUT(lklfsd.mounted_volume != NULL, STATUS_UNRECOGNIZED_VOLUME);
	
	CHECK_OUT(FLAG_ON(lklfsd.flags, VFS_UNLOAD_PENDING),STATUS_UNRECOGNIZED_VOLUME);
	
	DbgPrint("Mount volume");

	// if this succeeds...
	status = IoCreateDevice(lklfsd.driver, sizeof(LKLVCB), NULL,
			FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &volume_device);
	CHECK_OUT(!NT_SUCCESS(status), status);
	if (dev->AlignmentRequirement > volume_device->AlignmentRequirement)
			volume_device->AlignmentRequirement = dev->AlignmentRequirement;
	
	CLEAR_FLAG(volume_device->Flags, DO_DEVICE_INITIALIZING);
	volume_device->StackSize = (CCHAR)(dev->StackSize+1);

    CreateVcb(volume_device,dev,vpb,&AllocationSize);
	if(!FLAG_ON(((PLKLVCB)volume_device->DeviceExtension)->flags, VFS_VCB_FLAGS_VCB_INITIALIZED))
		TRY_RETURN(STATUS_INSUFFICIENT_RESOURCES);
    
	// yup, here we read the disk geometry
	ioctlSize = sizeof(DISK_GEOMETRY);
	status = BlockDeviceIoControl(dev, IOCTL_DISK_GET_DRIVE_GEOMETRY,
		NULL, 0, &((PLKLVCB)volume_device->DeviceExtension)->disk_geometry, &ioctlSize);
	CHECK_OUT(!NT_SUCCESS(status), status);

	ioctlSize = sizeof(PARTITION_INFORMATION);
	status = BlockDeviceIoControl(dev, IOCTL_DISK_GET_PARTITION_INFO,
		NULL, 0, &((PLKLVCB)volume_device->DeviceExtension)->partition_information, &ioctlSize);
	CHECK_OUT(!NT_SUCCESS(status), status);
	
	lklfsd.mounted_volume = volume_device;
		// try a linux mount
	status = run_linux_kernel(); // if this fails, then we fail to mount the volume
	((PLKLVCB)volume_device->DeviceExtension)->volume_path = "/";
	CHECK_OUT(!NT_SUCCESS(status), status);
	rc = sys_statfs_wrapper("/", &my_stat);
	
	vpb->DeviceObject = volume_device;
	// complete vpb fields from ?? --TODO--
	#define UNKNOWN_LABEL "unknown_label"
	CharToWchar(vpb->VolumeLabel, UNKNOWN_LABEL , sizeof(UNKNOWN_LABEL));
	vpb->VolumeLabel[sizeof(UNKNOWN_LABEL)] = 0;
	vpb->VolumeLabelLength = sizeof(UNKNOWN_LABEL)*2;
	vpb->SerialNumber = my_stat.f_type;

try_exit:

		if(!NT_SUCCESS(status))
		{
			if(volume_device) {
                 FreeVcb((PLKLVCB) volume_device->DeviceExtension);
				IoDeleteDevice(volume_device);
            }
			lklfsd.mounted_volume = NULL;
		}

	return status;
}

//
//	IRP_MN_MOUNT_VOLUME
//
NTSTATUS DDKAPI VfsMountVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
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
NTSTATUS DDKAPI VfsUserFileSystemRequest(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
	ULONG fs_ctrl_code = 0;
	PEXTENDED_IO_STACK_LOCATION stack_location_ex =(PEXTENDED_IO_STACK_LOCATION) stack_location;

	fs_ctrl_code = stack_location_ex->Parameters.FileSystemControl.FsControlCode;
	switch (fs_ctrl_code) {
	case FSCTL_LOCK_VOLUME:
		status = VfsLockVolume(irp, stack_location);
		break;
	case FSCTL_UNLOCK_VOLUME:
		status = VfsUnLockVolume(irp, stack_location);
		break;
	case FSCTL_DISMOUNT_VOLUME:
		status = VfsUnmountVolume(irp, stack_location);
		break;
	case FSCTL_IS_VOLUME_MOUNTED:
		status = VfsIsVolumeMounted(irp, stack_location);
		break;
	default:
		;
	}
	return status;
}

//
//	FSCTL_LOCK_VOLUME
//
NTSTATUS DDKAPI VfsLockVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PDEVICE_OBJECT device = NULL;
	PLKLVCB vcb;
	BOOLEAN notified = FALSE;
	BOOLEAN resource_acquired = FALSE;
	PFILE_OBJECT file_obj = NULL;

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
		VfsReportError("Volume already locked");
		TRY_RETURN(STATUS_ACCESS_DENIED);
	}

	// abort if open files still exist
	if (vcb->open_count) {
		VfsReportError("Open files still exist");
		TRY_RETURN(STATUS_ACCESS_DENIED);
	}

	// release lock
	RELEASE(&vcb->vcb_resource);
	resource_acquired = FALSE;

	// purge volume
	VfsPurgeVolume(vcb, TRUE);

    ExAcquireResourceExclusiveLite(&vcb->vcb_resource, TRUE);
	resource_acquired = TRUE;
	
	// if there are still open referneces we can't lock the volume
	if (vcb->reference_count > 1) {
		VfsReportError("Could not purge cached files");
		TRY_RETURN(STATUS_ACCESS_DENIED);
	}

	// set flag in both vcb and vpb structures
	SET_FLAG(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED);
	SetVpbFlag(vcb->vpb, VFS_VCB_FLAGS_VOLUME_LOCKED);
	DbgPrint("*** Volume LOCKED ***\n");
	status = STATUS_SUCCESS;
	
try_exit:

		if (resource_acquired)
			RELEASE(&vcb->vcb_resource);
		if (!NT_SUCCESS(status) && notified)
			FsRtlNotifyVolumeEvent(file_obj, FSRTL_VOLUME_LOCK_FAILED);

	return status;
}

VOID PurgeFile(PLKLFCB fcb, BOOLEAN flush_before_purge)
{
	IO_STATUS_BLOCK iosb;

	ASSERT(fcb);
	// BUG! BUG! BUG!
	if (flush_before_purge)
		CcFlushCache(&fcb->section_object, NULL, 0, &iosb);
	if (fcb->section_object.ImageSectionObject)
		MmFlushImageSection(&fcb->section_object, MmFlushForWrite);
	if (fcb->section_object.DataSectionObject)
		CcPurgeCacheSection(&fcb->section_object, NULL, 0, FALSE);
}

typedef struct _FCB_LIST_ENTRY {
	PLKLFCB fcb;
	LIST_ENTRY next;
} FCB_LIST_ENTRY, *PFCB_LIST_ENTRY;

//
//	purges any files that are still referenced, most probably by the cache mgr
//
VOID DDKAPI VfsPurgeVolume(PLKLVCB vcb, BOOLEAN flush_before_purge)
{
	BOOLEAN vcb_acquired = FALSE;
	PLKLFCB fcb = NULL;
	LIST_ENTRY fcb_list;
	PLIST_ENTRY entry = NULL;
	PFCB_LIST_ENTRY fcb_list_entry;


	ASSERT(vcb);
	// acquire vcb resource
	ExAcquireResourceSharedLite(&vcb->vcb_resource, TRUE);
	vcb_acquired = TRUE;
	
	// if volume is read only we cant purge it
	if (FLAG_ON(vcb->flags, VFS_VCB_FLAGS_VOLUME_READ_ONLY))
		flush_before_purge = FALSE;

	// for all the files's that haven't been dereferenced yet by the cache mgr
	InitializeListHead(&fcb_list);
	for (entry = vcb->fcb_list.Flink; entry != &vcb->fcb_list; entry = entry->Flink) {
		fcb = CONTAINING_RECORD(entry, LKLFCB, next);
		ExAcquireResourceExclusiveLite(&fcb->fcb_resource, TRUE);
		// reference them, so they dont get closed while we do the purge
		InterlockedIncrement(&fcb->reference_count);

		RELEASE(&fcb->fcb_resource);

		fcb_list_entry = ExAllocatePool(NonPagedPool, sizeof(FCB_LIST_ENTRY));
		InsertTailList(&fcb_list, &fcb_list_entry->next);
	}

	RELEASE(&vcb->vcb_resource);
	vcb_acquired = FALSE;

	// purge all files, after the purge, files will have a 0 reference count and they should be closed
	while (!IsListEmpty(&fcb_list)) {
		entry = RemoveHeadList(&fcb_list);
		fcb_list_entry = CONTAINING_RECORD(entry, struct _FCB_LIST_ENTRY, next);
		fcb = fcb_list_entry->fcb;

		PurgeFile(fcb, flush_before_purge);
		InterlockedDecrement(&fcb->reference_count);

		if (!fcb->reference_count) {
			FreeFcb(fcb);
		}
		ExFreePool(fcb_list_entry);
	}

	VfsReportError("Volume flushed and purged");

	if (vcb_acquired)
		RELEASE(&vcb->vcb_resource);

}

VOID SetVpbFlag(PVPB vpb,IN USHORT flag)
{
	KIRQL irql;
	IoAcquireVpbSpinLock(&irql);
	vpb->Flags |= flag;
	IoReleaseVpbSpinLock(irql);
}

VOID ClearVpbFlag(PVPB vpb,IN USHORT flag)
{
	KIRQL irql;
	IoAcquireVpbSpinLock(&irql);
	vpb->Flags &= ~flag;
	IoReleaseVpbSpinLock(irql);
}

NTSTATUS DDKAPI VfsUnLockVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	PDEVICE_OBJECT device = NULL;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PLKLVCB vcb = NULL;
	BOOLEAN vcb_acquired = FALSE;
	PFILE_OBJECT file_obj = NULL;


	device = stack_location->DeviceObject;
	CHECK_OUT(device == lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);

	vcb = (PLKLVCB)device->DeviceExtension;
	CHECK_OUT(vcb == NULL, STATUS_INVALID_PARAMETER);

	file_obj = stack_location->FileObject;
	CHECK_OUT(file_obj == NULL, STATUS_INVALID_PARAMETER);

	ExAcquireResourceExclusiveLite(&vcb->vcb_resource, TRUE);
	vcb_acquired = TRUE;

	if (!FLAG_ON(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED)) {
		VfsReportError("Volume is NOT LOCKED");
		TRY_RETURN(STATUS_ACCESS_DENIED);
	}

	CLEAR_FLAG(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED);
	ClearVpbFlag(vcb->vpb, VFS_VCB_FLAGS_VOLUME_LOCKED);
	DbgPrint("*** Volume UNLOCKED ***");

	status = STATUS_SUCCESS;
try_exit:

	if (vcb_acquired)
		RELEASE(&vcb->vcb_resource);
	FsRtlNotifyVolumeEvent(file_obj, FSRTL_VOLUME_UNLOCK);

	return status;
}

NTSTATUS LklUmount(IN PDEVICE_OBJECT dev,IN PFILE_OBJECT file)
{
	NTSTATUS status=STATUS_UNSUCCESSFUL;
	PLKLVCB vcb=NULL;
	BOOLEAN notified = FALSE;
	BOOLEAN vcb_acquired = FALSE;

	vcb=(PLKLVCB) dev->DeviceExtension;
	if (vcb == NULL)
	   return STATUS_INVALID_PARAMETER;
	   
	DbgPrint("Volume beeing DISMOUNTED");

	FsRtlNotifyVolumeEvent(file, FSRTL_VOLUME_DISMOUNT);
	notified = TRUE;
	ExAcquireResourceExclusiveLite(&vcb->vcb_resource, TRUE);
	vcb_acquired = TRUE;
	status=STATUS_SUCCESS;

	if (!FLAG_ON(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED)) {
			VfsReportError("Volume is NOT LOCKED");
			return(STATUS_ACCESS_DENIED);
		}

	//unmount volume  ( linux way )
	unload_linux_kernel();
	SET_FLAG(vcb->flags, VFS_VCB_FLAGS_BEING_DISMOUNTED);
	if (vcb_acquired)
			RELEASE(&vcb->vcb_resource);
	if (!NT_SUCCESS(status) && notified)
		FsRtlNotifyVolumeEvent(file, FSRTL_VOLUME_DISMOUNT_FAILED);
	return status;
}


NTSTATUS DDKAPI VfsUnmountVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	PDEVICE_OBJECT device = NULL;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PFILE_OBJECT file_obj = NULL;

	device = stack_location->DeviceObject;

	CHECK_OUT(device == lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);

	file_obj = stack_location->FileObject;
	status = LklUmount(device, file_obj);

try_exit:

	return status;
}

NTSTATUS DDKAPI VfsIsVolumeMounted(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	DbgPrint("Is volume mounted ?");
	return VfsVerifyVolume(irp, stack_location);
}

NTSTATUS DDKAPI VfsVerifyVolume(PIRP irp, PIO_STACK_LOCATION stack_location)
{
	PDEVICE_OBJECT device = NULL;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PLKLVCB vcb = NULL;
	BOOLEAN vcb_acquired = FALSE;

	DbgPrint("Verify volume");

	device = stack_location->DeviceObject;
	CHECK_OUT(device == lklfsd.device, STATUS_INVALID_DEVICE_REQUEST);

	vcb = (PLKLVCB) device->DeviceExtension;
	ASSERT(vcb);

	ExAcquireResourceExclusiveLite(&vcb->vcb_resource, TRUE);
	vcb_acquired = TRUE;

	CLEAR_FLAG(vcb->target_device->Flags, DO_VERIFY_VOLUME);

	status = STATUS_SUCCESS;
try_exit:

	if (vcb_acquired)
		RELEASE(&vcb->vcb_resource);

	return status;
}
