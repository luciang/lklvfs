/**
* read dispatch routine
* TODOs: 
* - volume read
**/

#include <lklvfs.h>

NTSTATUS DDKAPI VfsRead(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIRPCONTEXT irp_context;
	BOOLEAN top_level;

	ASSERT(device);
	ASSERT(irp);

	FsRtlEnterFileSystem();

	if (device == lklfsd.device) {
		LklCompleteRequest(irp, STATUS_INVALID_DEVICE_REQUEST);
		FsRtlExitFileSystem();
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	top_level = LklIsIrpTopLevel(irp);

	irp_context = AllocIrpContext(irp, device);
	if(irp_context == NULL) {
        LklCompleteRequest(irp, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	status = CommonRead(irp_context, irp);

	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;
}

NTSTATUS CommonRead(PIRPCONTEXT irp_context, PIRP irp)
{
	NTSTATUS status;
	PLKLFCB fcb = NULL;
	PLKLCCB ccb = NULL;
	PLKLVCB vcb = NULL;
	PFILE_OBJECT file = NULL;
	PIO_STACK_LOCATION stack_location;
	PUCHAR userBuffer;
	BOOLEAN complete_irp = FALSE;
	LARGE_INTEGER byte_offset;
	ULONG length;
	ULONG numberBytesRead = 0;
	BOOLEAN pagingIo;
	BOOLEAN nonBufferedIo;
	BOOLEAN syncIo;
	BOOLEAN vcb_resource_acq = FALSE;
	BOOLEAN fcb_resource_acq = FALSE;
	BOOLEAN paging_resource_acq = FALSE;

	stack_location =  IoGetCurrentIrpStackLocation(irp);
	ASSERT(stack_location);

	file = stack_location->FileObject;
	CHECK_OUT(file == NULL, STATUS_INVALID_PARAMETER);
   
	// If this happens to be an MDL read complete request, then
	// there is not much processing here.
	if (FLAG_ON(stack_location->MinorFunction,IRP_MN_COMPLETE)) {
		// Caller wants to tell the Cache Manager that a previously
		// allocated MDL can be freed.
		CcMdlReadComplete(file, irp->MdlAddress);
		irp->MdlAddress = NULL;
		// The IRP has been completed.
		complete_irp = FALSE;
		TRY_RETURN(STATUS_SUCCESS);
	}

	// If this is a request at IRQL DISPATCH_LEVEL, then post the request
	if (FLAG_ON(stack_location->MinorFunction,IRP_MN_DPC)) {
		DbgPrint("post request");
		complete_irp = FALSE;
		TRY_RETURN(STATUS_PENDING);
	}

	// Get the fcb and ccb pointers
	fcb = file->FsContext;
	ccb = file->FsContext2;
	CHECK_OUT(fcb == NULL, STATUS_INVALID_PARAMETER);
	CHECK_OUT(ccb == NULL, STATUS_INVALID_PARAMETER);

	// we need to check if the caller can block now, because sys_read is blocking
	if(!FLAG_ON(irp_context->flags, VFS_IRP_CONTEXT_CAN_BLOCK)) {
		complete_irp = FALSE;
		TRY_RETURN(STATUS_PENDING);
	}

	// Identify the type of read operation: a paging I/O operation, a normal noncached
	// operation, a non-MDL cached read operation, or an MDL read
	// operation
	pagingIo = FLAG_ON(irp->Flags, IRP_PAGING_IO);
	nonBufferedIo = FLAG_ON(irp->Flags, IRP_NOCACHE);
	syncIo= FLAG_ON(file->Flags, FO_SYNCHRONOUS_IO);

	// Verify that the read operation is allowed
	CHECK_OUT (length == 0, STATUS_SUCCESS);
	CHECK_OUT(FLAG_ON(fcb->flags, VFS_FCB_DIRECTORY), STATUS_INVALID_PARAMETER);

	// Obtain the starting offset, length, and buffer pointer supplied by the caller
	byte_offset = stack_location->Parameters.Read.ByteOffset;
	length = stack_location->Parameters.Read.Length;

	// We are asked to do a volume read
	if (fcb->id.type == VCB) {
		DbgPrint("VOLUME READ");
		//TODO: We need to send this on to the disk driver after validation of the offset and length.
		TRY_RETURN(STATUS_INVALID_PARAMETER);
	}

	CHECK_OUT(fcb->id.type != FCB || fcb->id.size !=sizeof(LKLFCB), STATUS_INVALID_PARAMETER);
	CHECK_OUT(ccb->id.type != CCB || ccb->id.size !=sizeof(LKLCCB), STATUS_INVALID_PARAMETER);

 	DbgPrint("READ");
	// Obtain any resources that are appropriate to ensure consistency of data.
	if (!pagingIo) {
		ExAcquireResourceSharedLite(&fcb->fcb_resource, TRUE);
		fcb_resource_acq = TRUE;
        } else {
		ExAcquireResourceSharedLite(&fcb->paging_resource, TRUE);
		paging_resource_acq = TRUE;
        }
 
	// Determine whether the byte range specified by the caller is valid, and if not,
	// return an appropriate error code to the caller.
	if ((byte_offset.QuadPart + (LONGLONG)length) > fcb->common_header.FileSize.QuadPart ) {
		if (byte_offset.QuadPart >= fcb->common_header.FileSize.QuadPart) {
			irp->IoStatus.Information = 0;
			TRY_RETURN(STATUS_END_OF_FILE);
		}
		length = (ULONG)(fcb->common_header.FileSize.QuadPart - byte_offset.QuadPart);
	}
	userBuffer = GetUserBuffer(irp);
	if (!nonBufferedIo) {
		// If this is a buffered I/O request and caching has not yet been initiated on the
		// FCB, invoke CcInitializeCacheMap to initiate caching at this time.
		if (file->PrivateCacheMap == NULL) {
			CcInitializeCacheMap(file, (PCC_FILE_SIZES)(&fcb->common_header.AllocationSize),
					     FALSE, &lklfsd.cache_mgr_callbacks, ccb);
		}
		//This is a regular cached I/O request. Let the Cache Manager worry about it.
		if (FLAG_ON(stack_location->MinorFunction, IRP_MN_MDL))	{
			CcMdlRead(file, &byte_offset, length, &irp->MdlAddress, &irp->IoStatus);
			status = irp->IoStatus.Status;
			numberBytesRead = irp->IoStatus.Information;
		} else {
			// If this is a buffered non-MDL I/O request, forward the request 
			// on to the NT Cache Manager via an invocation to CcCopyRead
			CcCopyRead(file, &(byte_offset), length, TRUE, userBuffer, &(irp->IoStatus));
			// We have the data
			status = irp->IoStatus.Status;
			numberBytesRead = irp->IoStatus.Information;
		}
		// update the offset
		file->CurrentByteOffset.QuadPart = byte_offset.QuadPart + numberBytesRead;
		sys_lseek_wrapper(ccb->fd, byte_offset.QuadPart + numberBytesRead, 0);
		
	} else {
		// Read from the disk
		if (byte_offset.LowPart!=0 || byte_offset.HighPart!=0) {
			//	if (byte_offset.LowPart == FILE_USE_FILE_POINTER_POSITION && byte_offset.HighPart == -1)
			//	;
			//	else {
					// lseek
					sys_lseek_wrapper(ccb->fd,byte_offset.LowPart, 0); 
			//	}
		}
		DbgPrint("Read from DISK");
		numberBytesRead = sys_read_wrapper(ccb->fd, userBuffer, length);
	}

	if(numberBytesRead >=0) {
		status = STATUS_SUCCESS;
		irp->IoStatus.Information = numberBytesRead;
	} else {
		status = STATUS_UNEXPECTED_IO_ERROR;
		irp->IoStatus.Information = 0;
	}
	
try_exit:
	// Once data has been obtained either from the Cache Manager or from lower level
	// drivers, release FCB resources acquired and return the results to the caller.
	if(vcb_resource_acq)
		RELEASE(&vcb->vcb_resource);
	if(fcb_resource_acq)
		RELEASE(&fcb->fcb_resource);
	if(paging_resource_acq)
		RELEASE(&fcb->paging_resource);
	if(status == STATUS_PENDING) {
		// lock user buffer 
		status = LockUserBuffer(irp, length, IoWriteAccess);
		if(NT_SUCCESS(status))
			status = LklPostRequest(irp_context, irp);
		else {
			LklCompleteRequest(irp, status);
			FreeIrpContext(irp_context);
		}
	} else {
		LklCompleteRequest(irp, status);
		FreeIrpContext(irp_context);
	}
	DbgPrint("Finished read");
	return status;
}

