/*
* close & it's friends
*/
#include <lklvfs.h>

NTSTATUS LklClose(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS					status;
	PIO_STACK_LOCATION			irpSp = NULL;
	PLKLVCB						vcb = NULL;
	PFILE_OBJECT				file = NULL;
	PLKLFCB						fcb = NULL;
	BOOLEAN						freeVcb = FALSE;
	BOOLEAN						vcbResourceAquired = FALSE;
	BOOLEAN						postRequest = FALSE;
	BOOLEAN						completeIrp = FALSE;
	ASSERT(device);
	ASSERT(irp);
	DbgPrint("CLOSE REQUEST");

	if(device == lklfsd.device) {
		// never fail for fs device
		FsRtlEnterFileSystem();
		LklCompleteRequest(irp,STATUS_SUCCESS);
		FsRtlExitFileSystem();

		return STATUS_SUCCESS;
	}
	// not fs device
	__try {
		vcb=(PLKLVCB) device->DeviceExtension;
		ASSERT(vcb);
		ASSERT(vcb->id.type == VCB && vcb->id.size == sizeof(LKLVCB));
		// cannot block in close
		if (!ExAcquireResourceExclusiveLite(&vcb->vcb_resource, FALSE)) {
				postRequest = TRUE;
				TRY_RETURN(STATUS_PENDING);
			}
		else {
				completeIrp = TRUE;
				vcbResourceAquired = TRUE;
			}

		irpSp = IoGetCurrentIrpStackLocation(irp);
		ASSERT(irpSp);

		// file object
		file = irpSp->FileObject;
		ASSERT(file);
		fcb = (PLKLFCB) file->FsContext;
		ASSERT(fcb);

		if (fcb->id.type == VCB)
		{
			DbgPrint("VOLUME CLOSE");
			vcb->reference_count--;
			if (!vcb->reference_count && FLAG_ON(vcb->flags, VFS_VCB_FLAGS_BEING_DISMOUNTED)) {
				freeVcb = TRUE;
			}
			TRY_RETURN(STATUS_SUCCESS);
		}
		status = STATUS_ACCESS_DENIED;

try_exit:
		;
	}
	__finally {
			if (vcbResourceAquired)
				RELEASE(&vcb->vcb_resource);
			if (postRequest) {
				DbgPrint("CLOSE POSTED REQUEST");
				status = LklPostRequest(irp);
			}
			else if (completeIrp && status != STATUS_PENDING) {
				LklCompleteRequest(irp, status);
			if (freeVcb)
				LklFreeVcb(vcb);
			}
	}
	return STATUS_SUCCESS;
}