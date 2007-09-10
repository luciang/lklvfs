/**
* all device control operations should be here
**/

#include <lklvfs.h>

NTSTATUS LklDeviceControl(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status=STATUS_SUCCESS;

	DbgPrint("Device Control");
	LklCompleteRequest(irp, status);
	return status;
}