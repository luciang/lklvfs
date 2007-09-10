#include <lklvfs.h>

NTSTATUS LklDeviceControl(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status=STATUS_SUCCESS;

	DbgPrint("Device COntrol");
	return status;
}