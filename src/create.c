/*
* create /open
*/

#include <lklvfs.h>

NTSTATUS LklCreate(PDEVICE_OBJECT device, PIRP irp)
{

	NTSTATUS					status = STATUS_SUCCESS;
	PIO_STACK_LOCATION			irpSp = NULL;
	PIO_SECURITY_CONTEXT		securityContext = NULL;
	PFILE_OBJECT				file = NULL;
	PFILE_OBJECT				relatedFile = NULL;
	ULONG						allocationSize = 0;
	PFILE_FULL_EA_INFORMATION	extAttrBuffer = NULL;
	ULONG						requestedOptions = 0;
	ULONG						requestedDisposition = 0;
	USHORT						fileAttributes = 0;
	USHORT						shareAccess = 0;
	ULONG						extAttrLength = 0;
	ACCESS_MASK					desiredAccess;
	BOOLEAN						defferedProcessing = FALSE;
	BOOLEAN						acquiredVcb = FALSE;
	BOOLEAN						directoryOnlyRequested = FALSE;
	BOOLEAN						fileOnlyRequested = FALSE;
	BOOLEAN						noBufferingSpecified = FALSE;
	BOOLEAN						writeThroughRequested = FALSE;
	BOOLEAN						deleteOnCloseSpecified = FALSE;
	BOOLEAN						noExtAttrKnowledge = FALSE;
	BOOLEAN						createTreeConnection = FALSE;
	BOOLEAN						openByFileId = FALSE;
	BOOLEAN						sequentialOnly = FALSE;
	BOOLEAN						randomAccess = FALSE;
	BOOLEAN						pageFileManipulation = FALSE;
	BOOLEAN						openTargetDirectory = FALSE;
	BOOLEAN						ignoreCaseWhenChecking = FALSE;
	UNICODE_STRING				targetObjectName;
	UNICODE_STRING				relatedObjectName;

	PLKLFCB						fcb;
	PLKLCCB						ccb;
	PLKLVCB						vcb;

	DbgPrint("OPEN REQUEST");

	FsRtlEnterFileSystem();
	// open on fs device
	__try {
		ASSERT(device);
		ASSERT(irp);

		if (device == lklfsd.device) {
			irp->IoStatus.Status = STATUS_SUCCESS;
			irp->IoStatus.Information = FILE_OPENED;
			TRY_RETURN(STATUS_SUCCESS);
		}

		irpSp = IoGetCurrentIrpStackLocation(irp);
		ASSERT(irpSp);

		// get caller supplied path
		file = irpSp->FileObject;						// the new file object
		targetObjectName = file->FileName;				// the name
		relatedFile = file->RelatedFileObject;	// the related file object

		if (relatedFile) {
			ccb = (PLKLCCB)(relatedFile->FsContext2);
			ASSERT(ccb);
			fcb = (PLKLFCB)(relatedFile->FsContext);
			ASSERT(fcb);
			ASSERT(fcb->id.type == FCB || fcb->id.type == VCB);
			relatedObjectName = relatedFile->FileName;
		}
		// TODO -- check file size
		// get arguments
		securityContext = irpSp->Parameters.Create.SecurityContext;
		desiredAccess = securityContext->DesiredAccess;
		requestedOptions = (irpSp->Parameters.Create.Options & FILE_VALID_OPTION_FLAGS);
		requestedDisposition = ((irpSp->Parameters.Create.Options >> 24) & 0xFF);
		fileAttributes	= (USHORT)(irpSp->Parameters.Create.FileAttributes & FILE_ATTRIBUTE_VALID_FLAGS);
		shareAccess	= irpSp->Parameters.Create.ShareAccess;
		extAttrBuffer = irp->AssociatedIrp.SystemBuffer;
		extAttrLength = irpSp->Parameters.Create.EaLength;
		sequentialOnly = ((requestedOptions & FILE_SEQUENTIAL_ONLY ) ? TRUE : FALSE);
		randomAccess = ((requestedOptions & FILE_RANDOM_ACCESS ) ? TRUE : FALSE);
		directoryOnlyRequested = ((requestedOptions & FILE_DIRECTORY_FILE) ? TRUE : FALSE);
		fileOnlyRequested = ((requestedOptions & FILE_NON_DIRECTORY_FILE) ? TRUE : FALSE);
		noBufferingSpecified = ((requestedOptions & FILE_NO_INTERMEDIATE_BUFFERING) ? TRUE : FALSE);
		writeThroughRequested = ((requestedOptions & FILE_WRITE_THROUGH) ? TRUE : FALSE);
		deleteOnCloseSpecified = ((requestedOptions & FILE_DELETE_ON_CLOSE) ? TRUE : FALSE);
		noExtAttrKnowledge = ((requestedOptions & FILE_NO_EA_KNOWLEDGE) ? TRUE : FALSE);
		createTreeConnection = ((requestedOptions & FILE_CREATE_TREE_CONNECTION) ? TRUE : FALSE);
		openByFileId = ((requestedOptions & FILE_OPEN_BY_FILE_ID) ? TRUE : FALSE);
		pageFileManipulation = ((irpSp->Flags & SL_OPEN_PAGING_FILE) ? TRUE : FALSE);
		openTargetDirectory = ((irpSp->Flags & SL_OPEN_TARGET_DIRECTORY) ? TRUE : FALSE);
		ignoreCaseWhenChecking = ((irpSp->Flags & SL_CASE_SENSITIVE) ? TRUE : FALSE);

		vcb = (PLKLVCB)(device->DeviceExtension);
		ASSERT(vcb);
		ASSERT(vcb->id.type == VCB);
		if (!file->Vpb) {
			file->Vpb = vcb->vpb;
		}

		//	acquire resource
		ExAcquireResourceExclusiveLite(&(vcb->vcb_resource), TRUE);
		acquiredVcb = TRUE;

		// if the volume has been locked, fail the request
		CHECK_OUT(vcb->flags & VFS_VCB_FLAGS_VOLUME_LOCKED, STATUS_ACCESS_DENIED);
		// check if it's a volume open request

		if ((file->FileName.Length == 0) && ((relatedFile == NULL) ||
			(fcb->id.type == VCB)))
		{
			CHECK_OUT(openTargetDirectory || extAttrBuffer, STATUS_INVALID_PARAMETER);
			CHECK_OUT(directoryOnlyRequested, STATUS_NOT_A_DIRECTORY);
			CHECK_OUT((requestedDisposition != FILE_OPEN) && (requestedDisposition != FILE_OPEN_IF),
				STATUS_ACCESS_DENIED);

			file->FsContext = vcb;
			vcb->reference_count++;
			irp->IoStatus.Information = FILE_OPENED;
			DbgPrint("VOLUME OPEN");
			TRY_RETURN(STATUS_SUCCESS);
		}

		// TODO -- more to do here

		status = STATUS_ACCESS_DENIED;
try_exit:
		;
	}
	__finally {
		// free used resources
		if(acquiredVcb)
			RELEASE(&vcb->vcb_resource);
	}

	LklCompleteRequest(irp,status);
	FsRtlExitFileSystem();
	return status;
}
