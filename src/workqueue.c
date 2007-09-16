/**
*	we use system worker threads for now
*	it will be better if we have our own pool of threads (?)
**/

#include <lklvfs.h>

NTSTATUS LklDispatchRequest(PIRPCONTEXT irp_context)
{
	ASSERT(irp_context);
	ASSERT((irp_context->id.type == IRP_CONTEXT) && (irp_context->id.size == sizeof(IRPCONTEXT)));

	switch (irp_context->major_function)
	{
	case IRP_MJ_CREATE:
		return CommonCreate(irp_context, irp_context->irp);

	case IRP_MJ_CLEANUP:
		return CommonCleanup(irp_context, irp_context->irp);

	case IRP_MJ_CLOSE:
		return CommonClose(irp_context, irp_context->irp);

	case IRP_MJ_DIRECTORY_CONTROL:
		return CommonDirectoryControl(irp_context, irp_context->irp);

	case IRP_MJ_LOCK_CONTROL:
	default:
		LklCompleteRequest(irp_context->irp, STATUS_INVALID_DEVICE_REQUEST);
		FreeIrpContext(irp_context);
	}
	return STATUS_NOT_IMPLEMENTED;
}


void DDKAPI LklDequeueRequest(IN PDEVICE_OBJECT device, IN PVOID context)
{
	NTSTATUS status;
	PIRPCONTEXT irp_context;

	irp_context = (PIRPCONTEXT) context;
	ASSERT(irp_context);
	ASSERT(irp_context->id.type == IRP_CONTEXT && irp_context->id.size == sizeof(IRPCONTEXT));

	IoFreeWorkItem(irp_context->work_item);

	if (FLAG_ON(irp_context->flags, VFS_IRP_CONTEXT_NOT_TOP_LEVEL))
		IoSetTopLevelIrp((PIRP)FSRTL_FSP_TOP_LEVEL_IRP);

	SET_FLAG(irp_context->flags, VFS_IRP_CONTEXT_CAN_BLOCK);

	FsRtlEnterFileSystem();

    status = LklDispatchRequest(irp_context);

	IoSetTopLevelIrp(NULL);
	FsRtlExitFileSystem();

}

NTSTATUS LklPostRequest(PIRPCONTEXT irp_context, PIRP irp)
{

	ASSERT(irp_context);
	ASSERT(irp_context->id.type == IRP_CONTEXT && irp_context->id.size == sizeof(IRPCONTEXT));
	IoMarkIrpPending(irp);
	
	irp_context->work_item = IoAllocateWorkItem(irp_context->target_device);
    IoQueueWorkItem(irp_context->work_item, LklDequeueRequest, CriticalWorkQueue ,irp_context);

	FreeIrpContext(irp_context);
	return STATUS_PENDING;
}
