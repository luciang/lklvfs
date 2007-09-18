/**
* write dispatch routine
* TODO: write
**/
#include <lklvfs.h>

NTSTATUS DDKAPI VfsWrite(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_NOT_IMPLEMENTED;
	BOOLEAN top_level;

	DbgPrint("WRITE");

	ASSERT(device);
	ASSERT(irp);

	FsRtlEnterFileSystem();

	if (device == lklfsd.device) {
		LklCompleteRequest(irp, STATUS_INVALID_DEVICE_REQUEST);
		FsRtlExitFileSystem();
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	top_level = LklIsIrpTopLevel(irp);
	LklCompleteRequest(irp, status);

	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;

}

NTSTATUS CommonWrite(PIRPCONTEXT irp_context, PIRP irp)
{

    return STATUS_NOT_IMPLEMENTED;
	
}
