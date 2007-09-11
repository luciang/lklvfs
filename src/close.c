/*
* close & it's friends
*/
#include <lklvfs.h>

NTSTATUS LklClose(PDEVICE_OBJECT device, PIRP irp)
{
	PLKLVCB vcb;

	ASSERT(device);
	ASSERT(irp);
	DbgPrint("CLOSE REQUEST");

	if(device == lklfsd.device) {
		FsRtlEnterFileSystem();
		IoCompleteRequest(irp, IO_DISK_INCREMENT);
		FsRtlExitFileSystem();

		return STATUS_SUCCESS;
	}

	vcb=(PLKLVCB) device->DeviceExtension;
	ASSERT(vcb);
	//TODO
	LklCompleteRequest(irp, STATUS_SUCCESS);
	return STATUS_SUCCESS;
}