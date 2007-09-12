/*
* create /open
*/

#include <lklvfs.h>

NTSTATUS LklOpenRootDirectory(PLKLVCB vcb, PIRP irp, USHORT share_access,
							  PIO_SECURITY_CONTEXT security_context, PFILE_OBJECT new_file_obj);
PLKLFCB LocateFcbInCore(PLKLVCB vcb, ULONG inode_no);

NTSTATUS LklVfsCreate(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	NTSTATUS exception;
	PIO_STACK_LOCATION stack_location = NULL;
	PIRPCONTEXT irp_context;
	BOOLEAN top_level;

	DbgPrint("OPEN");

	ASSERT(device);
	ASSERT(irp);

	FsRtlEnterFileSystem();
	top_level = LklIsIrpTopLevel(irp);

	__try {
		irp_context = AllocIrpContext(irp, device);
		ASSERT(irp_context);

		status = CommonCreate(irp_context, irp);
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		exception = GetExceptionCode();
			if(!NT_SUCCESS(status))
				DbgPrint("create: Exception %x ", exception);
	}

	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;
}


NTSTATUS CommonCreate(PIRPCONTEXT irp_context, PIRP irp)
{

	NTSTATUS					status = STATUS_SUCCESS;
	PIO_STACK_LOCATION			stack_location = NULL;
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
	BOOLEAN						postRequest = FALSE;
	UNICODE_STRING				targetObjectName;
	UNICODE_STRING				relatedObjectName;
	UNICODE_STRING				absolutePathName;
	PLKLFCB						fcb;
	PLKLCCB						ccb;
	PLKLVCB						vcb;

	absolutePathName.Buffer = NULL;
	absolutePathName.Length = 0;
	__try {

		if (irp_context->target_device == lklfsd.device) {
			irp->IoStatus.Status = STATUS_SUCCESS;
			irp->IoStatus.Information = FILE_OPENED;
			TRY_RETURN(STATUS_SUCCESS);
		}

		stack_location = IoGetCurrentIrpStackLocation(irp);
		ASSERT(stack_location);

		// get caller supplied path
		file = stack_location->FileObject;						// the new file object
		targetObjectName = file->FileName;				// the name
		relatedFile = file->RelatedFileObject;			// the related file object

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
		securityContext = stack_location->Parameters.Create.SecurityContext;
		desiredAccess = securityContext->DesiredAccess;
		requestedOptions = (stack_location->Parameters.Create.Options & FILE_VALID_OPTION_FLAGS);
		requestedDisposition = ((stack_location->Parameters.Create.Options >> 24) & 0xFF);
		fileAttributes	= (USHORT)(stack_location->Parameters.Create.FileAttributes & FILE_ATTRIBUTE_VALID_FLAGS);
		shareAccess	= stack_location->Parameters.Create.ShareAccess;
		extAttrBuffer = irp->AssociatedIrp.SystemBuffer;
		extAttrLength = stack_location->Parameters.Create.EaLength;
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
		pageFileManipulation = ((stack_location->Flags & SL_OPEN_PAGING_FILE) ? TRUE : FALSE);
		openTargetDirectory = ((stack_location->Flags & SL_OPEN_TARGET_DIRECTORY) ? TRUE : FALSE);
		ignoreCaseWhenChecking = ((stack_location->Flags & SL_CASE_SENSITIVE) ? TRUE : FALSE);

		vcb = (PLKLVCB)(irp_context->target_device->DeviceExtension);
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
		if ((targetObjectName.Length ==0) && ((relatedFile == NULL) ||
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
		CHECK_OUT(openByFileId, STATUS_ACCESS_DENIED);

		if(relatedFile) {
			//TODO
			// we stay away from this ( just for now )
			TRY_RETURN (STATUS_ACCESS_DENIED);
		}
		else {
			CHECK_OUT(targetObjectName.Buffer[0] != L'\\', STATUS_INVALID_PARAMETER);
			absolutePathName.MaximumLength = targetObjectName.Length;
			absolutePathName.Buffer = ExAllocatePoolWithTag(NonPagedPool, absolutePathName.MaximumLength, 'htpA');
			CHECK_OUT(!absolutePathName.Buffer, STATUS_INSUFFICIENT_RESOURCES);

			RtlZeroMemory(absolutePathName.Buffer, absolutePathName.MaximumLength);
			RtlCopyMemory((void *)(absolutePathName.Buffer), (void *)(targetObjectName.Buffer), targetObjectName.Length);
			absolutePathName.Length = targetObjectName.Length;
		}

		// for now we allow to open only the root directory
		if(absolutePathName.Length == 2) {
			CHECK_OUT(fileOnlyRequested || (requestedDisposition == FILE_SUPERSEDE) ||
				(requestedDisposition == FILE_OVERWRITE) ||
				(requestedDisposition == FILE_OVERWRITE_IF), STATUS_FILE_IS_A_DIRECTORY);

			DbgPrint("OPEN ROOT");
			status = LklOpenRootDirectory(vcb, irp, shareAccess, securityContext, file);
			if(NT_SUCCESS(status))
				irp->IoStatus.Information = FILE_OPENED;
			TRY_RETURN(status);
		}

		// search the file, bla, bla

		// get the fcb or create it

		// check previous open share access

		//allocate a new ccb

		// handle references count

		// init the new file object

		// set all atributes

		// allocate some data blocks if initial file size has been specified

		// rename / move

		status = STATUS_ACCESS_DENIED;
try_exit:
		;
	}
	__finally {
		// free used resources !!!! TODO

		if (absolutePathName.Buffer != NULL)
			ExFreePoolWithTag(absolutePathName.Buffer, 'htpA');

		if(acquiredVcb)
			RELEASE(&vcb->vcb_resource);

		if (!FLAG_ON(irp_context->flags, VFS_IRP_CONTEXT_EXCEPTION)) {
				FreeIrpContext(irp_context);
				LklCompleteRequest(irp, status);
		}
	}

	return status;
}

PLKLFCB LocateFcbInCore(PLKLVCB vcb, ULONG inode_no)
{
	PLKLFCB fcb = NULL;
	PLIST_ENTRY list_entry = NULL;

	if (IsListEmpty(&(vcb->fcb_list)))
		return NULL;
	for (list_entry = vcb->fcb_list.Flink; list_entry != &vcb->fcb_list; list_entry = list_entry->Flink) {
		fcb = CONTAINING_RECORD(list_entry, LKLFCB, next);
		ASSERT(fcb);
		if (fcb->ino == inode_no)
			return fcb;
	}
	return NULL;
}

NTSTATUS LklOpenRootDirectory(PLKLVCB vcb, PIRP irp, USHORT share_access,
							  PIO_SECURITY_CONTEXT security_context, PFILE_OBJECT new_file_obj)
{
	PLKLFCB fcb = NULL;
	PLKLCCB ccb = NULL;
	NTSTATUS status = STATUS_SUCCESS;
	PFSRTL_COMMON_FCB_HEADER header = NULL;
	USHORT root_ino;

	__try {
		// do we assert smth here?
		// get info about root inode -- st_ino
		/*
		root_ino = 0; // temporary
		fcb = LocateFcbInCore(vcb, root_ino);
		if (!fcb) {
			// create the root fcb

			status = LklCreateFcb(&fcb,new_file_obj, vcb, root_ino);
			header = &(fcb->common_header);
			header->IsFastIoPossible = FastIoIsNotPossible;
			header->Resource = &(fcb->fcb_resource);
			header->PagingIoResource = &(fcb->paging_resource);
			header->AllocationSize.QuadPart = 0; // get from stat: st_blksize * st_blocks
			header->FileSize.QuadPart = 0; // get st_size
			header->ValidDataLength.LowPart = 0xFFFFFFFF;
			header->ValidDataLength.HighPart = 0x7FFFFFFF;
			if (!NT_SUCCESS(status))
				TRY_RETURN(STATUS_INSUFFICIENT_RESOURCES);
		}
		ASSERT(fcb);

		status = LklCreateNewCcb(&ccb, fcb, new_file_obj);
		ASSERT(ccb);
		// open the file and ccb->fd =...

		new_file_obj->FsContext = fcb;
		new_file_obj->FsContext2 = ccb;
		new_file_obj->PrivateCacheMap = NULL;
		new_file_obj->SectionObjectPointer = &fcb->section_object;
		new_file_obj->Vpb = vcb->vpb;

try_exit:
		;
		*/
	}
	__finally {
		;
	}
	return status;
}