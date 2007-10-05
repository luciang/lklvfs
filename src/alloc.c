/**
* allocation related stuff
* put all TODOs here: -
**/

#include<lklvfs.h>

VOID CreateVcb(PDEVICE_OBJECT volume_dev, PDEVICE_OBJECT target_dev, PVPB vpb,
					  PLARGE_INTEGER alloc_size)
{
	NTSTATUS status = STATUS_SUCCESS;
	PLKLVCB vcb = NULL;

	vcb = (PLKLVCB)(volume_dev->DeviceExtension);
	RtlZeroMemory(vcb, sizeof(LKLVCB));

	vcb->id.type = VCB;
	vcb->id.size = sizeof(LKLVCB);

	status = ExInitializeResourceLite(&(vcb->vcb_resource));
	if(!NT_SUCCESS(status))
		return;

	vcb->target_device = target_dev;
	vcb->vcb_device = volume_dev;
	vcb->vpb = vpb;

	InitializeListHead(&(vcb->fcb_list));

	FsRtlNotifyInitializeSync(&vcb->notify_irp_mutex);
	InitializeListHead(&vcb->next_notify_irp);

	vcb->common_header.AllocationSize.QuadPart = alloc_size->QuadPart;
	vcb->common_header.FileSize.QuadPart = alloc_size->QuadPart;
	vcb->common_header.ValidDataLength.LowPart = 0xFFFFFFFF;
	vcb->common_header.ValidDataLength.HighPart = 0x7FFFFFFF;
	vcb->common_header.IsFastIoPossible = FastIoIsNotPossible;
	vcb->common_header.Resource = &(vcb->vcb_resource);

	ExAcquireResourceExclusiveLite(&(lklfsd.global_resource), TRUE);
	InsertTailList(&(lklfsd.vcb_list), &(vcb->next));
	RELEASE(&(lklfsd.global_resource));
	SET_FLAG(vcb->flags, VFS_VCB_FLAGS_VCB_INITIALIZED);
}

VOID FreeVcb(PLKLVCB vcb)
{
	if(vcb == NULL)
		return;
	if(vcb->id.type != VCB || vcb->id.size !=sizeof(LKLVCB))
		return;
	if(!FLAG_ON(vcb->flags, VFS_VCB_FLAGS_VCB_INITIALIZED))
		return;

	ClearVpbFlag(vcb->vpb, VPB_MOUNTED);

	ExAcquireResourceExclusiveLite(&lklfsd.global_resource, TRUE);
	RemoveEntryList(&vcb->next);
	RELEASE(&lklfsd.global_resource);

	ExDeleteResourceLite(&vcb->vcb_resource);
	FsRtlNotifyUninitializeSync(&vcb->notify_irp_mutex);
	
	//Don't free the device object here: we didn't allocate this object. Let the allocator free it if it finds this suitable.
	//IoDeleteDevice(vcb->vcb_device);
}

PLKLFCB AllocFcb()
{
	PLKLFCB fcb;

	fcb = ExAllocateFromNPagedLookasideList(fcb_cachep);
	if (!fcb)
		return NULL;
	
	RtlZeroMemory(fcb, sizeof(LKLFCB));
	fcb->id.type = FCB;
	fcb->id.size = sizeof(LKLFCB);
	fcb->flags = 0;
	
	return fcb;
}

VOID FreeFcb(PLKLFCB fcb)
{
     if(fcb == NULL)
            return;

     RtlFreeUnicodeString(&fcb->name);

     ExDeleteResourceLite(&fcb->fcb_resource);
     ExDeleteResourceLite(&fcb->paging_resource);
     ExFreeToNPagedLookasideList(fcb_cachep, fcb);
}


NTSTATUS CreateFcb(PLKLFCB *new_fcb, PFILE_OBJECT file_obj, PLKLVCB vcb, ULONG ino,
                   ULONG allocation, ULONG file_size)
{
	NTSTATUS status = STATUS_SUCCESS;
	PLKLFCB fcb = NULL;

	CHECK_OUT(!vcb, STATUS_INSUFFICIENT_RESOURCES);
	CHECK_OUT(!file_obj, STATUS_INSUFFICIENT_RESOURCES);

	fcb = AllocFcb();
	CHECK_OUT(!fcb, STATUS_INSUFFICIENT_RESOURCES);

	ExInitializeResourceLite(&(fcb->fcb_resource));
	ExInitializeResourceLite(&(fcb->paging_resource));
    
	fcb->vcb = vcb;
	fcb->name.Length = 0;
	fcb->name.Buffer = NULL;
	fcb->common_header.NodeTypeCode = (USHORT)FCB;
	fcb->common_header.NodeByteSize = sizeof(PLKLFCB);
	fcb->common_header.IsFastIoPossible = FastIoIsNotPossible;
	fcb->common_header.Resource = &(fcb->fcb_resource);
	fcb->common_header.PagingIoResource = &(fcb->paging_resource);
	fcb->common_header.ValidDataLength.LowPart = 0xFFFFFFFF;
	fcb->common_header.ValidDataLength.HighPart = 0x7FFFFFFF;
	fcb->common_header.AllocationSize.QuadPart = allocation; // get from stat: st_blksize * st_blocks
	fcb->common_header.FileSize.QuadPart = file_size; // get st_size
	
	fcb->section_object.DataSectionObject = NULL;
	fcb->section_object.SharedCacheMap = NULL;
	fcb->section_object.ImageSectionObject = NULL;
	
	InitializeListHead(&(fcb->ccb_list));
    
	InsertTailList(&(vcb->fcb_list), &(fcb->next));

	*new_fcb = fcb;
try_exit:

	return status;
}

PLKLCCB AllocCcb()
{
	PLKLCCB ccb = NULL;

	ccb = ExAllocateFromNPagedLookasideList(ccb_cachep);
	if (!ccb)
		return NULL;

	RtlZeroMemory(ccb, sizeof(LKLCCB));
	ccb->id.type = CCB;
	ccb->id.size = sizeof(LKLCCB);

	return ccb;
}

VOID CloseAndFreeCcb(PLKLCCB ccb)
{
     	if(ccb == NULL)
        	return;
	if(ccb->fd >0)
		sys_close_wrapper(ccb->fd);
	RtlFreeUnicodeString(&ccb->search_pattern);
	ExFreeToNPagedLookasideList(ccb_cachep, ccb);
}

NTSTATUS CreateNewCcb(PLKLCCB *new_ccb, PLKLFCB fcb, PFILE_OBJECT file_obj)
{
	PLKLCCB ccb = NULL;
	NTSTATUS status = STATUS_SUCCESS;

	ccb = AllocCcb();
	CHECK_OUT(!ccb, STATUS_INSUFFICIENT_RESOURCES);
	ccb->fcb = fcb;
	ccb->file_obj = file_obj;
	ccb->offset.QuadPart = 0;
	ccb->search_pattern.Length = 0;
	ccb->search_pattern.Buffer = NULL;
    
	InterlockedIncrement(&fcb->reference_count);
	InterlockedIncrement(&fcb->handle_count);
	InterlockedIncrement(&fcb->vcb->reference_count);

	fcb->vcb->open_count++;

	InsertTailList(&(fcb->ccb_list), &(ccb->next));

	*new_ccb = ccb;
try_exit:

	return status;
}

PIRPCONTEXT AllocIrpContext(PIRP irp, PDEVICE_OBJECT target_device)
{
	PIRPCONTEXT irp_context = NULL;
	PIO_STACK_LOCATION stack_location;

	irp_context = ExAllocateFromNPagedLookasideList(irp_context_cachep);
	if (!irp_context)
		return NULL;

	RtlZeroMemory(irp_context, sizeof(IRPCONTEXT));

	irp_context->id.type = IRP_CONTEXT;
	irp_context->id.size = sizeof(IRPCONTEXT);

	irp_context->irp = irp;
	irp_context->target_device = target_device;

	if (irp) {
		stack_location = IoGetCurrentIrpStackLocation(irp);
		ASSERT(stack_location);

		irp_context->major_function = stack_location->MajorFunction;
		irp_context->minor_function = stack_location->MinorFunction;

		if (stack_location->FileObject) {
			irp_context->file_object = stack_location->FileObject;
			// never block in close
			if (IoIsOperationSynchronous(irp) && 
			stack_location->MajorFunction != IRP_MJ_CLOSE )
				SET_FLAG(irp_context->flags, VFS_IRP_CONTEXT_CAN_BLOCK);

		} else
			SET_FLAG(irp_context->flags, VFS_IRP_CONTEXT_CAN_BLOCK);
	}

	if (IoGetTopLevelIrp() != irp)
		SET_FLAG(irp_context->flags, VFS_IRP_CONTEXT_NOT_TOP_LEVEL);

	return irp_context;
}

VOID FreeIrpContext(PIRPCONTEXT irp_context)
{
	ASSERT(irp_context);
	ExFreeToNPagedLookasideList(irp_context_cachep, irp_context);
}
