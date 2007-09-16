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

NTSTATUS CommonDirectoryControl(PIRPCONTEXT irp_context, PIRP irp)
{
	// TODO
	FreeIrpContext(irp_context);
	LklCompleteRequest(irp, STATUS_UNSUCCESSFUL);
	return STATUS_UNSUCCESSFUL;
}
