/*
* create /open
* put all TODOs here:
* - open file with sys_open and check if fd is valid
* - check file size
* - make a fstat
* - if abnormal termination call sys_close
* - uncomment the stat/open related lines
*/

#include <lklvfs.h>

NTSTATUS OpenRootDirectory(PLKLVCB vcb, PIRP irp, USHORT share_access,
							  PIO_SECURITY_CONTEXT security_context, PFILE_OBJECT new_file_obj);
PLKLFCB LocateFcbInCore(PLKLVCB vcb, ULONG inode_no);

NTSTATUS DDKAPI VfsCreate(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIRPCONTEXT irp_context;
	BOOLEAN top_level;

	DbgPrint("OPEN");

	ASSERT(device);
	ASSERT(irp);

	FsRtlEnterFileSystem();

	if (device == lklfsd.device) {
		irp->IoStatus.Information = FILE_OPENED;
		LklCompleteRequest(irp, STATUS_SUCCESS);
		FsRtlExitFileSystem();
		return STATUS_SUCCESS;
	}

	top_level = LklIsIrpTopLevel(irp);

		irp_context = AllocIrpContext(irp, device);
		ASSERT(irp_context);

		status = CommonCreate(irp_context, irp);

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
	PFILE_FULL_EA_INFORMATION	extAttrBuffer = NULL;
	ULONG						requestedOptions = 0;
	ULONG						requestedDisposition = 0;
	USHORT						fileAttributes = 0;
	USHORT						shareAccess = 0;
	ULONG						extAttrLength = 0;
	ACCESS_MASK					desiredAccess;
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
	UNICODE_STRING				absolutePathName;
	PLKLFCB						fcb;
	PLKLCCB						ccb;
	PLKLVCB						vcb;
	PLKLFCB						newFcb = NULL;
	PLKLCCB						newCcb = NULL;
	ULONG						fd;
	ULONG						ino = -1;
	ULONG						returnedInformation = -1;

	absolutePathName.Buffer = NULL;
	absolutePathName.Length = absolutePathName.MaximumLength = 0;
	fd = -1;

	stack_location = IoGetCurrentIrpStackLocation(irp);
	ASSERT(stack_location);

	// If the caller cannot block, post the request to be handled asynchronously
	if (! (irp_context->flags & VFS_IRP_CONTEXT_CAN_BLOCK) ) {
		// We must defer processing this request, since create/open is blocking
		status = LklPostRequest(irp_context,irp);
		TRY_RETURN(status);
	}

	// get caller supplied path
	file = stack_location->FileObject;				// the new file object
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
	// get absolute path name (?!)
	if(relatedFile) {
		// validity checks ...
		// TODO - check if the related fcb is a directory

		CHECK_OUT((relatedObjectName.Length==0) || (relatedObjectName.Buffer[0]!=L'\\'),
			STATUS_INVALID_PARAMETER);
		CHECK_OUT((targetObjectName.Length!=0) && (targetObjectName.Buffer[0]==L'\\'),
			STATUS_INVALID_PARAMETER);

		absolutePathName.MaximumLength = targetObjectName.Length + relatedObjectName.Length + sizeof(WCHAR);
		if (!(absolutePathName.Buffer = ExAllocatePool(NonPagedPool, absolutePathName.MaximumLength)))
			TRY_RETURN(STATUS_INSUFFICIENT_RESOURCES);
		RtlZeroMemory(absolutePathName.Buffer, absolutePathName.MaximumLength);

		RtlCopyMemory(absolutePathName.Buffer, relatedObjectName.Buffer, relatedObjectName.Length);
		absolutePathName.Length = relatedObjectName.Length;
		RtlAppendUnicodeToString(&absolutePathName, L"\\");
		RtlAppendUnicodeToString(&absolutePathName, targetObjectName.Buffer);
	}
	else {
		CHECK_OUT(targetObjectName.Buffer[0] != L'\\', STATUS_INVALID_PARAMETER);
		absolutePathName.MaximumLength = targetObjectName.Length;
		absolutePathName.Buffer = ExAllocatePool(NonPagedPool, absolutePathName.MaximumLength);
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
		status = OpenRootDirectory(vcb, irp, shareAccess, securityContext, file);
		if(NT_SUCCESS(status))
			irp->IoStatus.Information = FILE_OPENED;
		TRY_RETURN(status);
	}

	// search for the file, and open if it's there
	if (requestedDisposition == FILE_OPEN) {
		DbgPrint("Open this file");
		// fd = sys_open ...

		//CHECK_OUT((fd<0), STATUS_OBJECT_PATH_NOT_FOUND);
		TRY_RETURN(STATUS_OBJECT_PATH_NOT_FOUND);
	}
	else
	{
		// create and ... ?
		DbgPrint("Create/overwrite this file");
		TRY_RETURN(STATUS_ACCESS_DENIED);
	}

	// fstat to get inode number, size, etc. : ino = 
	newFcb = LocateFcbInCore(vcb,ino);
	if (!newFcb) {
		// create the fcb
		PFSRTL_COMMON_FCB_HEADER header = NULL;

		status = CreateFcb(&newFcb, file, vcb, ino);
		newFcb->name.Length = absolutePathName.Length;
		newFcb->name.MaximumLength = absolutePathName.MaximumLength;
		newFcb->name.Buffer = ExAllocatePool(NonPagedPool,absolutePathName.Length);
		RtlCopyMemory(newFcb->name.Buffer, absolutePathName.Buffer, absolutePathName.Length);
		if (!NT_SUCCESS(status))
			TRY_RETURN(STATUS_INSUFFICIENT_RESOURCES);
		// complete fcb fields
		header = &(fcb->common_header);
		
		// get the following info from stat struct
		header->AllocationSize.QuadPart = 0;
		header->FileSize.QuadPart = 0; // get st_size
		header->ValidDataLength.LowPart = 0xFFFFFFFF;
		header->ValidDataLength.HighPart = 0x7FFFFFFF;
	}
	//allocate a new ccb
	status = CreateNewCcb(&newCcb, newFcb, file);
	newCcb->fd = fd;
	// complete file object fields
	file->FsContext = newFcb;
	file->FsContext2 = newCcb;
	file->PrivateCacheMap = NULL;
	file->SectionObjectPointer = &newFcb->section_object;
	file->Vpb = vcb->vpb;
	// check access
	if (newFcb->handle_count > 0) {
		status = IoCheckShareAccess(desiredAccess, shareAccess, file, &newFcb->share_access, TRUE);
		CHECK_OUT(!NT_SUCCESS(status), status);
	} else 
		IoSetShareAccess(desiredAccess, shareAccess, file, &newFcb->share_access);
	// return FILE_OPENED if all's ok
	if (returnedInformation == -1)
		returnedInformation = FILE_OPENED;
try_exit:
	//don't forget to free used resources !!!!

	if (absolutePathName.Buffer != NULL)
		ExFreePool(absolutePathName.Buffer);

	if(acquiredVcb)
		RELEASE(&vcb->vcb_resource);
	if(status != STATUS_PENDING) {
		if(NT_SUCCESS(status)) {
			//If write-through was requested, then mark the file
			// object appropriately.
			if (writeThroughRequested)
				file->Flags |= FO_WRITE_THROUGH ;
		}
		if(!FLAG_ON(irp_context->flags, VFS_IRP_CONTEXT_EXCEPTION)) {
			irp->IoStatus.Information = returnedInformation;
			FreeIrpContext(irp_context);
			// complete the IRP
			LklCompleteRequest(irp,status);
		}
	}
	else
		DbgPrint("Post request");

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

NTSTATUS OpenRootDirectory(PLKLVCB vcb, PIRP irp, USHORT share_access,
							  PIO_SECURITY_CONTEXT security_context, PFILE_OBJECT new_file_obj)
{
	PLKLFCB fcb = NULL;
	PLKLCCB ccb = NULL;
	NTSTATUS status = STATUS_SUCCESS;
	PFSRTL_COMMON_FCB_HEADER header = NULL;
	USHORT root_ino;

	root_ino = 0; // temporary --> open root directory and stat to get inode number, if all it's ok then:
	fcb = LocateFcbInCore(vcb, root_ino);
	if (!fcb) {
		// create the root fcb

		status = CreateFcb(&fcb,new_file_obj, vcb, root_ino);
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

	status = CreateNewCcb(&ccb, fcb, new_file_obj);
	ASSERT(ccb);
	// ccb->fd =fd

	new_file_obj->FsContext = fcb;
	new_file_obj->FsContext2 = ccb;
	new_file_obj->PrivateCacheMap = NULL;
	new_file_obj->SectionObjectPointer = &fcb->section_object;
	new_file_obj->Vpb = vcb->vpb;

try_exit:
		;
		// if abnormal termination then close on fd

	return status;
}
