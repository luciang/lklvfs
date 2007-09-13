/*
* close & it's friends
*/
#include <lklvfs.h>

NTSTATUS LklVfsClose(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	NTSTATUS exception;
	PIO_STACK_LOCATION stack_location = NULL;
	PIRPCONTEXT irp_context;
	BOOLEAN top_level;

	DbgPrint("CLOSE");

	ASSERT(device);
	ASSERT(irp);

	FsRtlEnterFileSystem();
	top_level = LklIsIrpTopLevel(irp);

	__try {
		irp_context = AllocIrpContext(irp, device);
		ASSERT(irp_context);

		status = CommonClose(irp_context, irp);
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		exception = GetExceptionCode();
			if(!NT_SUCCESS(exception))
				DbgPrint("close: Exception %x ", exception);
	}

	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;
}

NTSTATUS CommonClose(PIRPCONTEXT irp_context, PIRP irp)
{
	NTSTATUS					status = STATUS_SUCCESS;
	PERESOURCE					resource_acquired = NULL;
	PIO_STACK_LOCATION			stack_location = NULL;
	PLKLVCB						vcb = NULL;
	PFILE_OBJECT				file = NULL;
	PLKLFCB						fcb = NULL;
	PLKLCCB						ccb;
	BOOLEAN						freeVcb = FALSE;
	BOOLEAN						vcbResourceAquired = FALSE;
	BOOLEAN						postRequest = FALSE;
	BOOLEAN						completeIrp = FALSE;

	__try {
		vcb=(PLKLVCB) irp_context->target_device->DeviceExtension;
		ASSERT(vcb);
		// make shure we have a vcb here
		ASSERT(vcb->id.type == VCB && vcb->id.size == sizeof(LKLVCB));

		// not fs device
		if(irp_context->target_device == lklfsd.device) {
			// never fail for fs device
			LklCompleteRequest(irp,STATUS_SUCCESS);
			TRY_RETURN(STATUS_SUCCESS);
		}

		// cannot block in close
		if (!ExAcquireResourceExclusiveLite(&vcb->vcb_resource, FALSE)) {
				postRequest = TRUE;
				TRY_RETURN(STATUS_PENDING);
			}
		else {
				completeIrp = TRUE;
				vcbResourceAquired = TRUE;
			}

		stack_location = IoGetCurrentIrpStackLocation(irp);
		ASSERT(stack_location);

		// file object
		file = stack_location->FileObject;
		ASSERT(file);
		fcb = (PLKLFCB) file->FsContext;
		ASSERT(fcb);
		ccb = (PLKLCCB) file->FsContext2;
		if (fcb->id.type == VCB)
		{
			DbgPrint("VOLUME CLOSE");
			InterlockedDecrement(&vcb->reference_count);
			if (!vcb->reference_count && FLAG_ON(vcb->flags, VFS_VCB_FLAGS_BEING_DISMOUNTED)) {
				freeVcb = TRUE;
			}
			TRY_RETURN(STATUS_SUCCESS);
		}

		 ASSERT((fcb->id.type == FCB) && (fcb->id.size == sizeof(LKLFCB)));

		// acquire fcb resource
		 if (!ExAcquireResourceExclusiveLite(&(fcb->fcb_resource), FALSE)) {
				postRequest = TRUE;
				TRY_RETURN(STATUS_PENDING);
			} else
				resource_acquired = &(fcb->fcb_resource);

		// free ccb
		ASSERT(ccb);
		RemoveEntryList(&ccb->next);

		LklCloseAndFreeCcb(ccb);

		file->FsContext2 = NULL;

		// decrement reference count
		ASSERT(fcb->reference_count);
		ASSERT(vcb->reference_count);
		InterlockedDecrement(&fcb->reference_count);
		InterlockedDecrement(&vcb->reference_count);

		// if fcb reference count == 0 release resource and free fcb; return success
		if (fcb->reference_count == 0) {
			RemoveEntryList(&fcb->next);
			RELEASE(resource_acquired);
			resource_acquired = NULL;
			LklFreeFcb(fcb);
		}
		completeIrp = TRUE;

try_exit:
		;
	}
	__finally {
		if (resource_acquired)
			RELEASE(resource_acquired);
		if (vcbResourceAquired)
			RELEASE(&vcb->vcb_resource);
		if (postRequest) {
			status = LklPostRequest(irp_context, irp);
		}
		else if (completeIrp && status != STATUS_PENDING) {
			LklCompleteRequest(irp, status);
			FreeIrpContext(irp_context);
		}
		if (freeVcb)
			LklFreeVcb(vcb);
	}

	return status;
}