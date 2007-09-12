#include <lklvfs.h>

NTSTATUS LklCleanup(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stack_location = NULL;
	PFILE_OBJECT file_obj = NULL;
	PLKLVCB	vcb = NULL;
	PLKLFCB fcb = NULL;
	BOOLEAN	vcbAquired = FALSE;

	DbgPrint("CLEANUP");
	__try {
		// always succed for fs device
		CHECK_OUT(device == lklfsd.device, STATUS_SUCCESS);
		vcb = (PLKLVCB) device->DeviceExtension;
		ASSERT(vcb);

		// stack location
		stack_location = IoGetCurrentIrpStackLocation(irp);
		ASSERT(stack_location);
		// TODO -- if the caller doesn't want blocking review this
		ExAcquireResourceExclusiveLite(&(vcb->vcb_resource), TRUE);
		vcbAquired = TRUE;
		// file object we're to clean
		file_obj = stack_location->FileObject;
		ASSERT(file_obj);
		fcb = file_obj->FsContext;
		ASSERT(fcb != NULL);

		if (fcb->id.type == VCB)
        {
            if (FLAG_ON(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED))
            {
                CLEAR_FLAG(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED);
                LklClearVpbFlag(vcb->vpb, VPB_LOCKED);
            }
            TRY_RETURN(STATUS_SUCCESS);
        }

try_exit:
		;
	}
	__finally {
		if (file_obj)
			SET_FLAG(file_obj->Flags, FO_CLEANUP_COMPLETE);
		if (vcbAquired)
			RELEASE(&vcb->vcb_resource);
		if (status != STATUS_PENDING)
			LklCompleteRequest(irp, status);
		// else: LklPostRequest
	}
	return status;
}