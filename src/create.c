/*
* create /open
* put all TODOs here:
* - check file size
* - set the rest of the flags in the new fcb
* - create, move, rename file
*/

#include <lklvfs.h>
#include <linux/stat.h>

NTSTATUS OpenRootDirectory(PLKLVCB vcb, PIRP irp, USHORT share_access,
							  PIO_SECURITY_CONTEXT security_context, PFILE_OBJECT new_file_obj);
PLKLFCB LocateFcbInCore(PLKLVCB vcb, ULONG inode_no);

NTSTATUS DDKAPI VfsCreate(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIRPCONTEXT irp_context;
	BOOLEAN top_level;

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
	LONG						fd = -1;
	LONG						ino = -1;
	LONG                        rc;
	ULONG						returnedInformation = -1;
    PSTR                        unixPath;
    STATS                       mystat;
    
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
		CHECK_OUT(ccb == NULL, STATUS_DRIVER_INTERNAL_ERROR);
		fcb = (PLKLFCB)(relatedFile->FsContext);
		CHECK_OUT(fcb == NULL, STATUS_DRIVER_INTERNAL_ERROR);
		CHECK_OUT(fcb->id.type != FCB && fcb->id.type != VCB, STATUS_INVALID_PARAMETER);
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
	CHECK_OUT(vcb == NULL, STATUS_DRIVER_INTERNAL_ERROR);
	CHECK_OUT(vcb->id.type != VCB, STATUS_INVALID_PARAMETER);

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

		TRY_RETURN(STATUS_SUCCESS);
	}
	CHECK_OUT(openByFileId, STATUS_ACCESS_DENIED);
	// get absolute path name (?!)
	if(relatedFile) {
		// validity checks ...
		// check if the related fcb is a directory
		CHECK_OUT(!FLAG_ON(fcb->flags, VFS_FCB_DIRECTORY), STATUS_INVALID_PARAMETER);
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
	    VfsCopyUnicodeString(&absolutePathName, &targetObjectName);
		CHECK_OUT(!absolutePathName.Buffer, STATUS_INSUFFICIENT_RESOURCES);
	}

	// for now we allow to open only the root directory
	if(absolutePathName.Length == 2) {
		CHECK_OUT(fileOnlyRequested || (requestedDisposition == FILE_SUPERSEDE) ||
			(requestedDisposition == FILE_OVERWRITE) ||
			(requestedDisposition == FILE_OVERWRITE_IF), STATUS_FILE_IS_A_DIRECTORY);

		status = OpenRootDirectory(vcb, irp, shareAccess, securityContext, file);
		if(NT_SUCCESS(status))
			irp->IoStatus.Information = FILE_OPENED;
		TRY_RETURN(status);
	}

	if (openTargetDirectory)
	{
			//TODO: rename/move
			TRY_RETURN(STATUS_NOT_IMPLEMENTED);
	}
	// open file
	if (requestedDisposition == FILE_OPEN) {
		unixPath =  VfsCopyUnicodeStringToZcharUnixPath(&absolutePathName);
 	    DbgPrint("Open file %s", unixPath);
		CHECK_OUT(unixPath == NULL, STATUS_INSUFFICIENT_RESOURCES);
		
		if (directoryOnlyRequested)
		   fd = sys_open_wrapper(unixPath, O_RDONLY|O_DIRECTORY|O_LARGEFILE, 0666);
        else
           fd = sys_open_wrapper(unixPath, O_RDONLY|O_LARGEFILE, 0666);
        ExFreePool(unixPath);
        CHECK_OUT((fd<=0), STATUS_OBJECT_PATH_NOT_FOUND);
	}
	else
	{
		// create and ... ?
		unixPath =  VfsCopyUnicodeStringToZcharUnixPath(&absolutePathName);
		CHECK_OUT(unixPath == NULL, STATUS_INSUFFICIENT_RESOURCES);
		DbgPrint("Create/overwrite file %s", unixPath);
	    ExFreePool(unixPath);
		TRY_RETURN(STATUS_NOT_IMPLEMENTED);
	}
	
	// fstat to get inode number, size, etc.
	rc = sys_newfstat_wrapper(fd, &mystat);
    CHECK_OUT(rc<0, STATUS_OBJECT_PATH_NOT_FOUND);
  
    ino = mystat.st_ino;
    
	newFcb = LocateFcbInCore(vcb,ino);
	
	if (!newFcb) {
		// create the fcb

		status = CreateFcb(&newFcb, file, vcb, ino, mystat.st_blksize * mystat.st_blocks, mystat.st_size);
		if (!NT_SUCCESS(status))
			TRY_RETURN(STATUS_INSUFFICIENT_RESOURCES);
	    VfsCopyUnicodeString(&newFcb->name, &absolutePathName);
		CHECK_OUT(!absolutePathName.Buffer, STATUS_INSUFFICIENT_RESOURCES);
		// complete fcb fields
		//set all the flags in the fcb structure
		if (writeThroughRequested)
			SET_FLAG(newFcb->flags, VFS_FCB_WRITE_THROUGH);
        if(deleteOnCloseSpecified)
            SET_FLAG(newFcb->flags, VFS_FCB_DELETE_ON_CLOSE);
            
        //set here VFS_FCB_DIRECTORY if the inode is a directory
        if( S_ISDIR(mystat.st_mode)) {
            SET_FLAG(newFcb->flags, VFS_FCB_DIRECTORY);
            }
	}
	//allocate a new ccb
	status = CreateNewCcb(&newCcb, newFcb, file);
	CHECK_OUT(!NT_SUCCESS(status), status);
	
	newCcb->fd = fd;
	// complete file object fields
	file->FsContext = newFcb;
	file->FsContext2 = newCcb;
	file->PrivateCacheMap = NULL;
	file->SectionObjectPointer = &newFcb->section_object;
	file->Vpb = vcb->vpb;
	// check access
    // TODO:  i have a bug here and dunno what it is

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
		else {
             // we failed in a way or another
             if( fd >0) {
                 sys_close_wrapper(fd);
                 if(newCcb)
                     newCcb->fd = -1;
             }
             if(newFcb) {
                 RemoveEntryList(&newFcb->next);
                 FreeFcb(newFcb);
             }
             if(newCcb) {
                 RemoveEntryList(&newCcb->next);
                 CloseAndFreeCcb(newCcb);
             }
        }
		irp->IoStatus.Information = returnedInformation;
		FreeIrpContext(irp_context);
		// complete the IRP
		LklCompleteRequest(irp,status);
	}
	else
		LklPostRequest(irp_context, irp);

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
	USHORT root_ino;
    LONG fd = -1;
    struct stat mystat;
    
	root_ino = 0; 
    //open root directory
    fd = sys_open_wrapper("/", O_RDONLY|O_LARGEFILE|O_DIRECTORY, 0);
    CHECK_OUT(fd < 0, STATUS_OBJECT_PATH_NOT_FOUND);
    // stat to get some info about inode
    sys_newfstat_wrapper(fd, &mystat);
    root_ino = mystat.st_ino;
    
	fcb = LocateFcbInCore(vcb, root_ino);
	if (!fcb) {
		// create the root fcb

		status = CreateFcb(&fcb,new_file_obj, vcb, root_ino, mystat.st_blksize * mystat.st_blocks, mystat.st_size);
		if (!NT_SUCCESS(status))
			TRY_RETURN(STATUS_INSUFFICIENT_RESOURCES);

		SET_FLAG(fcb->flags, VFS_FCB_DIRECTORY);
		SET_FLAG(fcb->flags, VFS_FCB_ROOT_DIRECTORY);
	
	}
	CHECK_OUT(fcb == NULL, STATUS_DRIVER_INTERNAL_ERROR);

	status = CreateNewCcb(&ccb, fcb, new_file_obj);
	CHECK_OUT(ccb == NULL, STATUS_INSUFFICIENT_RESOURCES);
    ccb->fd =fd;

	new_file_obj->FsContext = fcb;
	new_file_obj->FsContext2 = ccb;
	new_file_obj->PrivateCacheMap = NULL;
	new_file_obj->SectionObjectPointer = &fcb->section_object;
	new_file_obj->Vpb = vcb->vpb;

try_exit:

		// if abnormal termination then close on fd
		if(!NT_SUCCESS(status)) {
            if(fd >0)
                  sys_close_wrapper(fd);
        }
	return status;
}
