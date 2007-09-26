/**
* cleanup stuff
* put all TODOs here:
**/
#include <lklvfs.h>

NTSTATUS DDKAPI VfsCleanup(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIRPCONTEXT irp_context;
	BOOLEAN top_level;

	DbgPrint("CLEANUP");

	FsRtlEnterFileSystem();
	top_level = LklIsIrpTopLevel(irp);

	irp_context = AllocIrpContext(irp, device);
	ASSERT(irp_context);

	status = CommonCleanup(irp_context, irp);


	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;

}

NTSTATUS CommonCleanup(PIRPCONTEXT irp_context, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stack_location = NULL;
	PERESOURCE resource_acquired = NULL;
	IO_STATUS_BLOCK iosb;
	PFILE_OBJECT file = NULL;
	PLKLVCB	vcb = NULL;
	PLKLFCB fcb = NULL;
	BOOLEAN	vcb_acquired = FALSE;
	BOOLEAN post_request = FALSE;
	BOOLEAN canWait = FALSE;

	// always succed for fs device
	CHECK_OUT(irp_context->target_device == lklfsd.device, STATUS_SUCCESS);
	vcb = (PLKLVCB)irp_context->target_device->DeviceExtension;
	ASSERT(vcb);

	// stack location
	stack_location = IoGetCurrentIrpStackLocation(irp);
	ASSERT(stack_location);

	canWait = ((irp_context->flags & VFS_IRP_CONTEXT_CAN_BLOCK) ? TRUE : FALSE);

	// get vcb resource ex
	if (!ExAcquireResourceExclusiveLite(&(vcb->vcb_resource), canWait)) {
		post_request = TRUE;
		TRY_RETURN(STATUS_PENDING);
	} else
		vcb_acquired = TRUE;

	// file object we're to clean
	file = stack_location->FileObject;
	ASSERT(file);
	fcb = file->FsContext;
	ASSERT(fcb);
	// clean on volume object
	if (fcb->id.type == VCB)
    {
        if (FLAG_ON(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED))
        {
            CLEAR_FLAG(vcb->flags, VFS_VCB_FLAGS_VOLUME_LOCKED);
            ClearVpbFlag(vcb->vpb, VPB_LOCKED);
        }
        TRY_RETURN(STATUS_SUCCESS);
    }

	if (!ExAcquireResourceExclusiveLite(&(fcb->fcb_resource), canWait)) {
			post_request = TRUE;
			TRY_RETURN(STATUS_PENDING);
		}
	else
		resource_acquired = &(fcb->fcb_resource);

	CHECK_OUT(fcb->handle_count== 0, STATUS_INVALID_PARAMETER);

	fcb->handle_count--;
	vcb->open_count--;

	//invoking the flush call explicitly could be useful
	if (file->PrivateCacheMap != NULL)
				CcFlushCache(file->SectionObjectPointer, NULL, 0, &iosb);

	//uninitialize cache map even if caching hasn't been initialized
	CcUninitializeCacheMap(file, NULL, NULL);

	IoRemoveShareAccess(file, &fcb->share_access);

	if (fcb->handle_count == 0)
        {
            if (FLAG_ON(fcb->flags, VFS_FCB_DELETE_PENDING))
            {
				DbgPrint("DELETE PENDIND SET IN CLEANUP");

                //must delete this file ??
            }
        }

	/*If the cleanup operation is for a directory, we have to complete any
	pending notify IRPs for the file object.*/
/*	if (FLAG_ON(fcb->flags, VFS_FCB_DIRECTORY))
       FsRtlNotifyCleanup(&vcb->notify_irp_mutex, &vcb->next_notify_irp, file->FsContext2);
*/
try_exit:

	if (file)
		SET_FLAG(file->Flags, FO_CLEANUP_COMPLETE);
	if (resource_acquired)
		RELEASE(resource_acquired);
	if (vcb_acquired)
		RELEASE(&vcb->vcb_resource);
	if (post_request) {
            DbgPrint("Post cleanup request");
			status = LklPostRequest(irp_context, irp);
        }
	if (status != STATUS_PENDING){
		FreeIrpContext(irp_context);
		LklCompleteRequest(irp, status);
	}

	return status;
}
