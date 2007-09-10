/*
* close & it's friends
*/
#include <lklvfs.h>

NTSTATUS LklClose(PDEVICE_OBJECT device, PIRP irp)
{
	ASSERT(device);
	ASSERT(irp);
	DbgPrint("CLOSE REQUEST");
	if(device == lklfsd.device)
	{
		FsRtlEnterFileSystem();
		IoCompleteRequest(irp, IO_DISK_INCREMENT);
		FsRtlExitFileSystem();
		IoUnregisterFileSystem(lklfsd.device);
		IoDeleteDevice(lklfsd.device);
		return STATUS_SUCCESS;
	}
	//TODO

	return STATUS_ACCESS_DENIED;
}