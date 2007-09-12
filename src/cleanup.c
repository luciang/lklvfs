#include <lklvfs.h>

NTSTATUS LklVfsCleanup(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	NTSTATUS exception;
	PIO_STACK_LOCATION stack_location = NULL;
	PIRPCONTEXT irp_context;
	BOOLEAN top_level;

	DbgPrint("CLEANUP");

	FsRtlEnterFileSystem();
	top_level = LklIsIrpTopLevel(irp);

	__try {
		irp_context = AllocIrpContext(irp, device);
		ASSERT(irp_context);

		status = CommonCleanup(irp_context, irp);
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		exception = GetExceptionCode();
			if(!NT_SUCCESS(exception))
				DbgPrint("clean: Exception %x ", exception);
	}

	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;

}

NTSTATUS CommonCleanup(PIRPCONTEXT irp_context, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stack_location = NULL;
	PFILE_OBJECT file_obj = NULL;
	PLKLVCB	vcb = NULL;
	PLKLFCB fcb = NULL;
	BOOLEAN	vcb_acquired = FALSE;
	BOOLEAN post_request = FALSE;

	__try {
		// always succed for fs device
		CHECK_OUT(irp_context->target_device == lklfsd.device, STATUS_SUCCESS);
		vcb = (PLKLVCB)irp_context->target_device->DeviceExtension;
		ASSERT(vcb);

		// stack location
		stack_location = IoGetCurrentIrpStackLocation(irp);
		ASSERT(stack_location);
		// get vcb resource ex
		if (!ExAcquireResourceExclusiveLite(&(vcb->vcb_resource), FALSE)) {
			post_request = TRUE;
			TRY_RETURN(STATUS_PENDING);
		} else
			vcb_acquired = TRUE;

		// file object we're to clean
		file_obj = stack_location->FileObject;
		ASSERT(file_obj);
		fcb = file_obj->FsContext;
		ASSERT(fcb);

		if (fcb->id.type == VCB)
        {
            if (FLAG_ON(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED))
            {
                CLEAR_FLAG(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED);
                LklClearVpbFlag(vcb->vpb, VPB_LOCKED);
            }
            TRY_RETURN(STATUS_SUCCESS);
        }
		// TODO -- ok, it's not a vcb, so it must be a fcb
		// and do the required cleanup for a fcb

try_exit:
		;
	}
	__finally {
		if (file_obj)
			SET_FLAG(file_obj->Flags, FO_CLEANUP_COMPLETE);
		if (vcb_acquired)
			RELEASE(&vcb->vcb_resource);
		if (post_request)
				status = LklPostRequest(irp_context, irp);
		if (status != STATUS_PENDING){
			FreeIrpContext(irp_context);
			LklCompleteRequest(irp, status);
		}
	}

	return status;
}