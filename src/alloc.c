/**
* allocation related stuff
**/

#include<lklvfs.h>

void LklCreateVcb(PDEVICE_OBJECT volume_dev, PDEVICE_OBJECT target_dev, PVPB vpb,
					  PLARGE_INTEGER alloc_size)
{
	NTSTATUS status = STATUS_SUCCESS;
	PLKLVCB vcb = NULL;

	vcb = (PLKLVCB)(volume_dev->DeviceExtension);
	RtlZeroMemory(vcb, sizeof(LKLVCB));

	vcb->id.type = VCB;
	vcb->id.size = sizeof(LKLVCB);

	status = ExInitializeResourceLite(&(vcb->vcb_resource));
	ASSERT(NT_SUCCESS(status));

	vcb->target_device = target_dev;
	vcb->vcb_device = volume_dev;
	vcb->vpb = vpb;

	InitializeListHead(&(vcb->fcb_list));
	InitializeListHead(&(vcb->next_notify_irp));

	KeInitializeMutex(&(vcb->notify_irp_mutex), 0);

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

void LklFreeVcb(PLKLVCB vcb)
{
	LklClearVpbFlag(vcb->vpb, VPB_MOUNTED);

	ExAcquireResourceExclusiveLite(&lklfsd.global_resource, TRUE);
	RemoveEntryList(&vcb->next);
	RELEASE(&lklfsd.global_resource);

	ExDeleteResourceLite(&vcb->vcb_resource);

	IoDeleteDevice(vcb->vcb_device);
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

	return fcb;
}

void VfsFreeFcb(PLKLFCB fcb)
{
	ASSERT(fcb);

	ExDeleteResourceLite(&fcb->fcb_resource);
	ExDeleteResourceLite(&fcb->paging_resource);
	ExFreeToNPagedLookasideList(fcb_cachep, fcb);
}


NTSTATUS LklCreateFcb(PLKLFCB *new_fcb, PFILE_OBJECT file_obj, PLKLVCB vcb, ULONG ino)
{
	NTSTATUS status = STATUS_SUCCESS;
	PLKLFCB fcb = NULL;

	ASSERT(vcb);
	ASSERT(file_obj);

	__try
	{
		fcb = AllocFcb();
		CHECK_OUT(!fcb, STATUS_INSUFFICIENT_RESOURCES);

		ExInitializeResourceLite(&(fcb->fcb_resource));
		ExInitializeResourceLite(&(fcb->paging_resource));

		fcb->vcb = vcb;
		InsertTailList(&(vcb->fcb_list), &(fcb->next));

		InitializeListHead(&(fcb->ccb_list));

		*new_fcb = fcb;
try_exit:
		;
	}
	__finally
	{
	}
	return status;
}

PLKLCCB AllocCcb()
{
	PLKLCCB ccb = NULL;
	// think again
	ccb = ExAllocateFromNPagedLookasideList(ccb_cachep);
	if (!ccb)
		return NULL;

	RtlZeroMemory(ccb, sizeof(LKLCCB));
	ccb->id.type = CCB;
	ccb->id.size = sizeof(LKLCCB);

	return ccb;
}

void VfsCloseAndFreeCcb(PLKLCCB ccb)
{
	ASSERT(ccb);

//	sys_close(ccb->fd);
	ExFreeToNPagedLookasideList(ccb_cachep, ccb);
}

NTSTATUS LklCreateNewCcb(PLKLCCB *new_ccb, PLKLFCB fcb, PFILE_OBJECT file_obj)
{
	PLKLCCB ccb = NULL;
	NTSTATUS status = STATUS_SUCCESS;

	__try
	{
		ccb = AllocCcb();
		CHECK_OUT(!ccb, STATUS_INSUFFICIENT_RESOURCES);
		ccb->fcb = fcb;
		ccb->file_obj = file_obj;
		ccb->offset.QuadPart = 0;

		// ccb->fd = sys_open ( ... ) - path must be changed
		InterlockedIncrement(&fcb->reference_count);
		InterlockedIncrement(&fcb->handle_count);
		InterlockedIncrement(&fcb->vcb->reference_count);

		fcb->vcb->open_count++;

		InsertTailList(&(fcb->ccb_list), &(ccb->next));

		*new_ccb = ccb;
try_exit:
		;
	}
	__finally
	{
	}
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
			if (IoIsOperationSynchronous(irp))
				SET_FLAG(irp_context->flags, VFS_IRP_CONTEXT_CAN_BLOCK);

		} else
			SET_FLAG(irp_context->flags, VFS_IRP_CONTEXT_CAN_BLOCK);
	}

	if (IoGetTopLevelIrp() != irp)
		SET_FLAG(irp_context->flags, VFS_IRP_CONTEXT_NOT_TOP_LEVEL);

	return irp_context;
}

void FreeIrpContext(PIRPCONTEXT irp_context)
{
	ASSERT(irp_context);
	ExFreeToNPagedLookasideList(irp_context_cachep, irp_context);
}