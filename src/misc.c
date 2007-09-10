#include<lklvfs.h>

BOOLEAN LklIsIrpTopLevel(PIRP irp)
{
	if (IoGetTopLevelIrp() == NULL) {
		IoSetTopLevelIrp(irp);
		return TRUE;
	}
	return FALSE;
}

void LklCompleteRequest(PIRP irp, NTSTATUS status)
{
	if (irp != NULL) {
		if (NT_ERROR(status) && FLAG_ON(irp->Flags, IRP_INPUT_OPERATION))
			irp->IoStatus.Information = 0;
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_DISK_INCREMENT);
	}
	return;
}

NTSTATUS LklDummyIrp(PDEVICE_OBJECT dev_obj, PIRP irp)
{
	BOOLEAN top_level = FALSE;
	NTSTATUS status = STATUS_NOT_IMPLEMENTED;

	DbgPrint("****** DUMMY IRP Handler *******");

	FsRtlEnterFileSystem();

	top_level = LklIsIrpTopLevel(irp);

	irp->IoStatus.Status = status;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_DISK_INCREMENT);

	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;
}
