/**
* directory control operations
* TODO -CommonDirectoryControl and all that it's related to this
**/
#include <lklvfs.h>


NTSTATUS DDKAPI VfsDirectoryControl(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIRPCONTEXT irp_context = NULL;
	BOOLEAN top_level = FALSE;
	
	DbgPrint("Directory Control");	

	FsRtlEnterFileSystem();

	top_level = LklIsIrpTopLevel(irp);

	irp_context = AllocIrpContext(irp, device);
	status = CommonDirectoryControl(irp_context, irp);

	if (top_level) 
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;
}

NTSTATUS VfsQueryDirectory(PIRPCONTEXT irp_context, PIRP irp,PIO_STACK_LOCATION stack_location,
          PFILE_OBJECT file_obj, PLKLFCB fcb, PLKLCCB ccb)
{
          NTSTATUS status = STATUS_NOT_IMPLEMENTED;
          BOOLEAN canWait;

    	CHECK_OUT(fcb->id.type == VCB || !(fcb->flags & VFS_FCB_DIRECTORY), STATUS_INVALID_PARAMETER);
    
    	// If the caller cannot block, post the request to be handled asynchronously
    	canWait = FLAG_ON(irp_context->flags, VFS_IRP_CONTEXT_CAN_BLOCK);
    	
    	if (!canWait) {
    		status = LklPostRequest(irp_context,irp);
    		TRY_RETURN(status);
    	}
	
try_exit:
          LklCompleteRequest(irp, status);
          FreeIrpContext(irp_context);
          
          return status;
}

NTSTATUS CommonDirectoryControl(PIRPCONTEXT irp_context, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stack_location = NULL;
	PFILE_OBJECT file_obj = NULL;
	PLKLFCB fcb = NULL;
	PLKLCCB ccb = NULL;

	stack_location = IoGetCurrentIrpStackLocation(irp);
	file_obj = stack_location->FileObject;
	fcb = (PLKLFCB)(file_obj->FsContext);
	CHECK_OUT(fcb == NULL, STATUS_INVALID_PARAMETER);
	ccb = (PLKLCCB)(file_obj->FsContext2);
	CHECK_OUT(ccb == NULL, STATUS_INVALID_PARAMETER);

	switch (stack_location->MinorFunction) {
	case IRP_MN_QUERY_DIRECTORY:
		status = VfsQueryDirectory(irp_context, irp, stack_location, file_obj, fcb, ccb);
		break;
	case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
		status = STATUS_NOT_IMPLEMENTED;
		break;
	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}
try_exit:

	LklCompleteRequest(irp, status);
	FreeIrpContext(irp_context);

	return status;
}

