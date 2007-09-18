/*
* close & it's friends
* TODOs: -
*/
#include <lklvfs.h>

NTSTATUS DDKAPI VfsClose(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIRPCONTEXT irp_context;
	BOOLEAN top_level;

	DbgPrint("CLOSE");

	ASSERT(device);
	ASSERT(irp);

	FsRtlEnterFileSystem();

	if (device == lklfsd.device) {
		LklCompleteRequest(irp, STATUS_SUCCESS);
		FsRtlExitFileSystem();
		return STATUS_SUCCESS;
	}

	top_level = LklIsIrpTopLevel(irp);

	irp_context = AllocIrpContext(irp, device);
	if(irp_context == NULL)
		TRY_RETURN(STATUS_INSUFFICIENT_RESOURCES);

	status = CommonClose(irp_context, irp);

try_exit:
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

	vcb=(PLKLVCB) irp_context->target_device->DeviceExtension;
	CHECK_OUT(vcb == NULL, STATUS_DRIVER_INTERNAL_ERROR);
	// make shure we have a vcb here
	CHECK_OUT(!(vcb->id.type == VCB && vcb->id.size == sizeof(LKLVCB)), STATUS_INVALID_PARAMETER);

	stack_location = IoGetCurrentIrpStackLocation(irp);
	ASSERT(stack_location);

	// never make a close request block
	if (!ExAcquireResourceExclusiveLite(&vcb->vcb_resource, FALSE)) {
			postRequest = TRUE;
			TRY_RETURN(STATUS_PENDING);
		}
	else {
			completeIrp = TRUE;
			vcbResourceAquired = TRUE;
		}

	// file object
	file = stack_location->FileObject;
	ASSERT(file);
	fcb = (PLKLFCB) file->FsContext;
	CHECK_OUT(fcb == NULL, STATUS_INVALID_PARAMETER);
	
	// volume close
	if (fcb->id.type == VCB)
	{
		DbgPrint("VOLUME CLOSE");
		InterlockedDecrement(&vcb->reference_count);
		if (!vcb->reference_count && FLAG_ON(vcb->flags, VFS_VCB_FLAGS_BEING_DISMOUNTED)) {
			freeVcb = TRUE;
		}
		completeIrp = TRUE;
		TRY_RETURN(STATUS_SUCCESS);
	}

	 CHECK_OUT(!((fcb->id.type == FCB) && (fcb->id.size == sizeof(LKLFCB))), STATUS_INVALID_PARAMETER);
	 ccb = (PLKLCCB) file->FsContext2;
	 CHECK_OUT(ccb == NULL, STATUS_INVALID_PARAMETER);

	// acquire fcb resource -  never block in close
	 if (!ExAcquireResourceExclusiveLite(&(fcb->fcb_resource), FALSE)) {
			postRequest = TRUE;
			TRY_RETURN(STATUS_PENDING);
		} else
			resource_acquired = &(fcb->fcb_resource);

	// free ccb
	RemoveEntryList(&ccb->next);

	CloseAndFreeCcb(ccb);

	file->FsContext2 = NULL;

	// decrement reference count
	CHECK_OUT(!fcb->reference_count, STATUS_DRIVER_INTERNAL_ERROR);
	CHECK_OUT(!vcb->reference_count, STATUS_DRIVER_INTERNAL_ERROR);
	InterlockedDecrement(&fcb->reference_count);
	InterlockedDecrement(&vcb->reference_count);

	// if fcb reference count == 0 release resource and free fcb; return success
	if (fcb->reference_count == 0) {
		RemoveEntryList(&fcb->next);
		RELEASE(resource_acquired);
		resource_acquired = NULL;
		FreeFcb(fcb);
	}

	completeIrp = TRUE;

try_exit:
     
	if (resource_acquired)
		RELEASE(resource_acquired);
	if (vcbResourceAquired)
		RELEASE(&vcb->vcb_resource);
	if (postRequest) {
		DbgPrint("post request");
		status = LklPostRequest(irp_context, irp);
	}
	else if (completeIrp && status != STATUS_PENDING) {
		LklCompleteRequest(irp, status);
		FreeIrpContext(irp_context);
	}
	if (freeVcb)
		FreeVcb(vcb);

	return status;
}
