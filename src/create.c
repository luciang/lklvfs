/*
* create /open
*/

#include <lklvfs.h>

NTSTATUS LklCreate(PDEVICE_OBJECT device, PIRP irp)
{

	ASSERT(device);
	ASSERT(irp);
	DbgPrint("OPEN REQUEST");
	if (device == lklfsd.device) {
		FsRtlEnterFileSystem();
		irp->IoStatus.Status = STATUS_SUCCESS;
		irp->IoStatus.Information = FILE_OPENED;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		FsRtlExitFileSystem();
		return STATUS_SUCCESS;
	}
	// TODO
	LklCompleteRequest(irp,STATUS_SUCCESS);
	return STATUS_SUCCESS;
}
