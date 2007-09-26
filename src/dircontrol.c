/*
* directory control operations
* TODO 
**/
#include <lklvfs.h>
#include <linux/stat.h>

NTSTATUS DDKAPI VfsDirectoryControl(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIRPCONTEXT irp_context = NULL;
	BOOLEAN top_level = FALSE;
	
	FsRtlEnterFileSystem();

	top_level = LklIsIrpTopLevel(irp);

	irp_context = AllocIrpContext(irp, device);
	status = CommonDirectoryControl(irp_context, irp);

	if (top_level) 
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;
}

struct dirent_buffer {
	void * buffer;
	FILE_INFORMATION_CLASS info_class;
	ULONG used_length;
	ULONG query_block_length;
	NTSTATUS status;
	BOOLEAN first_time_query;
	BOOLEAN return_single_entry;
	PUNICODE_STRING search_pattern;
	ULONG buffer_length;
	int fd;
	PULONG next_entry_offset;
};


#define BEGIN_CASE(x, X) \
	case File##x##Information:\
	{\
		PFILE_##X##_INFORMATION info=(PFILE_##X##_INFORMATION) (((char*)buf->buffer)+buf->used_length);\
		int __tmp=buf->used_length;\
		\
		info->FileIndex = (ULONG)offset;\
		
#define END_CASE(x, X) \
		buf->used_length += sizeof(FILE_##X##_INFORMATION);\
		buf->used_length += buf->used_length % 8;\
		info->NextEntryOffset = buf->used_length - __tmp;\
		buf->next_entry_offset = &info->NextEntryOffset;\
		break;\
	}

#define FillStandardWinInfo	\
	RtlSecondsSince1970ToTime (mystat.st_ctime, &info->CreationTime);	\
	RtlSecondsSince1970ToTime (mystat.st_atime, &info->LastAccessTime); \
	RtlSecondsSince1970ToTime (mystat.st_mtime, &info->LastWriteTime); \
	RtlSecondsSince1970ToTime (mystat.st_mtime, &info->ChangeTime); \
	info->EndOfFile.QuadPart = mystat.st_size;	\
	info->AllocationSize.QuadPart = mystat.st_blksize * mystat.st_blocks;
	
//FiXME: symbolic links are always threated as files, even if they point to a directory
#define FillFileType \
	info->FileAttributes = FILE_ATTRIBUTE_NORMAL;	\
	if (S_ISDIR(mystat.st_mode)) {	\
		SET_FLAG(info->FileAttributes, FILE_ATTRIBUTE_DIRECTORY);	\
	}

// FIXME: short name
#define FillShortName \
	info->ShortNameLength = (namlen < 12 ? namlen : 12) * 2; \
	RtlCopyMemory(info->ShortName, file_name.Buffer, info->ShortNameLength); 

#define FillFileName \
	info->FileNameLength = namlen * 2; \
	RtlCopyMemory(info->FileName, file_name.Buffer, namlen * 2); \
	buf->used_length += namlen * 2 ; 

#define FillEa \
	info->EaSize = 0;

#define FillId \
	info->FileId.QuadPart = mystat.st_ino;

NTSTATUS filldir(struct dirent_buffer * buf, IN PCHAR name, int namlen, ULONG offset, PLKLFCB fcb)
{
    UNICODE_STRING file_name;
    STATS mystat;
    INT ret = 0;
    PSTR path;
    
    if (!offset && buf->used_length != 0) {
       buf->status = STATUS_INVALID_PARAMETER;
       return STATUS_INVALID_PARAMETER;
    }
    // if i have smth in the buffer and it's required a single entry then we must return
	if (buf->used_length && buf->return_single_entry) {
		buf->status = STATUS_SUCCESS;
		return STATUS_INVALID_PARAMETER;
	}

	if (buf->buffer_length < (buf->query_block_length + namlen * 2 + buf->used_length)) {
		if (!buf->used_length) {
			buf->status = STATUS_INFO_LENGTH_MISMATCH;
		} else {
			buf->status = STATUS_SUCCESS;;
		}
		return STATUS_INVALID_PARAMETER;
	}
    
	file_name.Length = file_name.MaximumLength = namlen * 2;
	file_name.Buffer = ExAllocatePoolWithTag(NonPagedPool, namlen*2, 'LTCD');
	CharToWchar(file_name.Buffer, (char*)name, namlen);
    // make the full path for stat
    path = VfsCopyUnicodeStringToZcharUnixPath(fcb->vcb->linux_device.mnt, fcb->vcb->linux_device.mnt_length,
           &fcb->name, name, namlen);
           
    if(!path) {
        buf->status = STATUS_INSUFFICIENT_RESOURCES;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    DbgPrint("filldir(path='%.255s', offset=%d)\n",
		path,(int)offset);
    ret = sys_newstat_wrapper(path, &mystat);
    ExFreePool(path);
 
    if(ret<0) {
        buf->status = STATUS_DISK_CORRUPT_ERROR;
        return STATUS_DISK_CORRUPT_ERROR;
    }

	if (FsRtlDoesNameContainWildCards(buf->search_pattern) ?
		FsRtlIsNameInExpression(buf->search_pattern, &file_name, FALSE, NULL) :
		!RtlCompareUnicodeString(buf->search_pattern, &file_name, TRUE)) 
	{

		switch(buf->info_class) {

		BEGIN_CASE(Directory, DIRECTORY)
			FillStandardWinInfo;
			FillFileType;
			FillFileName;
		END_CASE(Directory, DIRECTORY)

		BEGIN_CASE(BothDirectory, BOTH_DIR)
			FillStandardWinInfo;
			FillFileType;
			FillEa;
			FillShortName;
			FillFileName;
		END_CASE(BothDirectory, BOTH_DIR)

		BEGIN_CASE(Names, NAMES)
			FillFileName;
		END_CASE(Names, NAMES)

		BEGIN_CASE(FullDirectory, FULL_DIR)
			FillStandardWinInfo;
			FillFileType;
			FillEa;
			FillFileName;
		END_CASE(FullDirectory, FULL_DIR)
		
		BEGIN_CASE(IdFullDirectory, ID_FULL_DIRECTORY)
			FillStandardWinInfo;
			FillFileType;
			FillEa;
			FillFileName;
			FillId;
		END_CASE(IdFullDirectory, FULL_DIRECTORY)

		BEGIN_CASE(IdBothDirectory, ID_BOTH_DIRECTORY)
			FillStandardWinInfo;
			FillFileType;
			FillEa;
			FillShortName;
			FillFileName;
			FillId;
		END_CASE(IdBothDirectory, ID_BOTH_DIRECTORY)
		
		default:
            buf->status = STATUS_INVALID_INFO_CLASS;
			return STATUS_INVALID_INFO_CLASS;
		}
    }
    
    RtlFreeUnicodeString(&file_name);
	
	buf->status = STATUS_SUCCESS;
	return STATUS_SUCCESS;
}

NTSTATUS VfsQueryDirectory(PIRPCONTEXT irp_context, PIRP irp,PIO_STACK_LOCATION stack_location,
          PFILE_OBJECT file_obj, PLKLFCB fcb, PLKLCCB ccb)
{
	NTSTATUS status = STATUS_NOT_IMPLEMENTED;
	BOOLEAN canWait = FALSE;
	BOOLEAN restart_scan = FALSE;
	BOOLEAN index_specified = FALSE;
	BOOLEAN fcb_resource_acq = FALSE;
	BOOLEAN postRequest = FALSE;
	ULONG file_index = 0;
	ULONG starting_index_for_search;
	PEXTENDED_IO_STACK_LOCATION stack_location_ex = (PEXTENDED_IO_STACK_LOCATION) stack_location;
	struct dirent_buffer win_buffer;
    PDIRENT lin_buffer = NULL;
    PDIRENT de;
    ULONG reclen;
    LONG rc;
    
    CHECK_OUT(fcb->id.type == VCB, STATUS_INVALID_PARAMETER);
//    name_string = VfsCopyUnicodeStringToZcharUnixPath(vcb->linux_device.mnt, vcb->linux_device.mnt_length, &fcb->name);
 //   DbgPrint("Query directory %s", name_string);
  //  ExFreePool(name_string);
	CHECK_OUT(!FLAG_ON(fcb->flags, VFS_FCB_DIRECTORY), STATUS_INVALID_PARAMETER);
   
	// If the caller cannot block, post the request to be handled asynchronously
	canWait = FLAG_ON(irp_context->flags, VFS_IRP_CONTEXT_CAN_BLOCK);
	
	if (!canWait) {
		postRequest = TRUE;
		TRY_RETURN(STATUS_PENDING);
	}

	// Get the callers parameters
	win_buffer.first_time_query = FALSE;
	win_buffer.buffer_length = stack_location_ex->Parameters.QueryDirectory.Length;
	win_buffer.search_pattern = (PUNICODE_STRING)stack_location_ex->Parameters.QueryDirectory.FileName;
	file_index = stack_location_ex->Parameters.QueryDirectory.FileIndex;
	
	switch ((win_buffer.info_class = stack_location_ex->Parameters.QueryDirectory.FileInformationClass)) {
		case FileDirectoryInformation:
			win_buffer.query_block_length = sizeof(FILE_DIRECTORY_INFORMATION);
			break;
		case FileBothDirectoryInformation:
			win_buffer.query_block_length = sizeof(FILE_BOTH_DIR_INFORMATION);
			break;
		case FileNamesInformation:
			win_buffer.query_block_length = sizeof(FILE_NAMES_INFORMATION);
			break;
		case FileFullDirectoryInformation:
			win_buffer.query_block_length = sizeof(FILE_FULL_DIR_INFORMATION);
			break;
		case FileIdFullDirectoryInformation:
             win_buffer.query_block_length = sizeof(FILE_ID_FULL_DIRECTORY_INFORMATION);
             break;
        case FileIdBothDirectoryInformation:
             win_buffer.query_block_length = sizeof(FILE_ID_BOTH_DIRECTORY_INFORMATION);
             break;
		default:
 
			TRY_RETURN(STATUS_INVALID_INFO_CLASS);
		}

	restart_scan = stack_location->Flags & SL_RESTART_SCAN;
	win_buffer.return_single_entry = stack_location->Flags & SL_RETURN_SINGLE_ENTRY;
	index_specified = stack_location->Flags & SL_INDEX_SPECIFIED;

	// Acquire the FCB resource; if the caller cannot block, post the request
	if(!ExAcquireResourceSharedLite(&fcb->fcb_resource, canWait)) {
		postRequest = TRUE;
		TRY_RETURN(STATUS_PENDING);
	}
	fcb_resource_acq = TRUE;

	//Get the users buffer
	win_buffer.buffer = GetUserBuffer(irp);
	CHECK_OUT(win_buffer.buffer == NULL, STATUS_INVALID_USER_BUFFER);
	
	if (win_buffer.search_pattern != NULL) {
			if (ccb->search_pattern.Length == 0)
				win_buffer.first_time_query = TRUE;
			else
				RtlFreeUnicodeString(&ccb->search_pattern);

       VfsCopyUnicodeString(&ccb->search_pattern, win_buffer.search_pattern);
    }
    else if (ccb->search_pattern.Length == 0) {
		RtlInitUnicodeString(&ccb->search_pattern, L"*");
			win_buffer.first_time_query = TRUE;
    } 
    else {
		win_buffer.search_pattern = &ccb->search_pattern;
    }
    
    if (index_specified) {
		DbgPrint("ne-a dat file index: %d\n", file_index);
		starting_index_for_search = file_index; // start from the specified index
		} else if (restart_scan) {
			DbgPrint("Restart scan req \n");
			starting_index_for_search = 0; // start from the beginning
		} else {
			starting_index_for_search = ccb->offset.LowPart; // start from where we left
		}
		
    RtlZeroMemory(win_buffer.buffer, win_buffer.buffer_length);

    if (win_buffer.buffer_length < win_buffer.query_block_length)
			TRY_RETURN(STATUS_INFO_LENGTH_MISMATCH);
    win_buffer.used_length = 0;
    win_buffer.fd = ccb->fd;
	win_buffer.next_entry_offset=0;
	
	// get the dentries from this directory
	lin_buffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, 'ridL');
	CHECK_OUT(lin_buffer == NULL, STATUS_INSUFFICIENT_RESOURCES);
	rc = sys_getdents_wrapper(win_buffer.fd,(PDIRENT) lin_buffer, PAGE_SIZE);
	CHECK_OUT(rc<0, STATUS_INVALID_PARAMETER);

     // starting from starting_index_search we fill win_buffer
     de = (PDIRENT)((char*)lin_buffer+starting_index_for_search);
     while(starting_index_for_search < rc) {
          reclen=de->d_reclen;
	      status = filldir(&win_buffer, de->d_name, reclen, starting_index_for_search, fcb);
	      if(!NT_SUCCESS(status))
               break;

          de=(PDIRENT)((char*)de+reclen); 
          starting_index_for_search+=reclen;
	}
    ccb->offset.LowPart = starting_index_for_search;
 
    sys_lseek_wrapper(win_buffer.fd, 0, 0);
    
    if (win_buffer.next_entry_offset)
			*win_buffer.next_entry_offset=0;

	if (!win_buffer.used_length) {
			if (win_buffer.first_time_query)
				status = STATUS_NO_SUCH_FILE;
			else
				status = STATUS_NO_MORE_FILES;
		} 
        else {
			status = win_buffer.status;
		}
 try_exit:  
    if(lin_buffer)
         ExFreePool(lin_buffer);
    if (fcb_resource_acq)
		 RELEASE(&fcb->fcb_resource);
    if(postRequest) {
         status = LklPostRequest(irp_context, irp);
    }
    else {
         LklCompleteRequest(irp, status);
         FreeIrpContext(irp_context);
    }
    return status;
}

NTSTATUS VfsNotifyDirectory(PIRPCONTEXT irp_context, PIRP irp, PIO_STACK_LOCATION stack_location,
							PFILE_OBJECT file_obj, PLKLFCB fcb, PLKLCCB ccb)
{
	NTSTATUS status = STATUS_NOT_IMPLEMENTED;
	BOOLEAN canWait = FALSE;
	BOOLEAN fcb_resource_acq = FALSE;
	BOOLEAN postReq = FALSE;
	BOOLEAN watchTree = FALSE;
	BOOLEAN postRequest = FALSE;
	ULONG completionFilter;
	PLKLVCB vcb;
	PEXTENDED_IO_STACK_LOCATION stack_location_ex = (PEXTENDED_IO_STACK_LOCATION) stack_location;
    DbgPrint("Notify directory control");

	// Validate the fcb: we accept notify request only on directories
	CHECK_OUT(fcb->id.type == VCB || !FLAG_ON(fcb->flags, VFS_FCB_DIRECTORY), STATUS_INVALID_PARAMETER);
	canWait = FLAG_ON(irp_context->flags, VFS_IRP_CONTEXT_CAN_BLOCK);
	vcb = fcb->vcb;

	// Acquire the fcb resource shared
	if (!ExAcquireResourceSharedLite(&(fcb->fcb_resource), canWait)) {
		postReq = TRUE;
		TRY_RETURN(STATUS_PENDING);
	}
	fcb_resource_acq = TRUE;

	// Obtain some parameters sent by the caller
	completionFilter = stack_location_ex->Parameters.NotifyDirectory.CompletionFilter;
	watchTree = (stack_location_ex->Flags & SL_WATCH_TREE ? TRUE : FALSE);
    //  If the file is marked as DELETE_PENDING then complete this
    //  request immediately.
    //
    
	FsRtlNotifyFullChangeDirectory(&vcb->notify_irp_mutex,
                                   &vcb->next_notify_irp,
									(PVOID) ccb,
                                    (PSTRING) &fcb->name,
                                    watchTree,
									FALSE,
                                    completionFilter,
                                    irp,
                                    NULL,
                                    NULL);
	status = STATUS_PENDING;

try_exit:

	if(fcb_resource_acq)
			RELEASE(&fcb->fcb_resource);

	if(postRequest) {
		status = LklPostRequest(irp_context, irp);
	}
	else {
		if(status != STATUS_PENDING) {
			LklCompleteRequest(irp, status);
			FreeIrpContext(irp_context);
		}
		else
			FreeIrpContext(irp_context);
	}

	return status;
}

NTSTATUS CommonDirectoryControl(PIRPCONTEXT irp_context, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stack_location = NULL;
	PFILE_OBJECT file_obj = NULL;
	PLKLFCB fcb = NULL;
	PLKLCCB ccb = NULL;

	stack_location = IoGetCurrentIrpStackLocation(irp);
	ASSERT(stack_location);
	file_obj = stack_location->FileObject;
	ASSERT(file_object);
	fcb = (PLKLFCB)(file_obj->FsContext);
	ccb = (PLKLCCB)(file_obj->FsContext2);
	if(fcb == NULL || ccb == NULL) {
        LklCompleteRequest(irp, STATUS_INVALID_PARAMETER);
		FreeIrpContext(irp_context);
        return STATUS_INVALID_PARAMETER;
     }

	switch (stack_location->MinorFunction) {
	case IRP_MN_QUERY_DIRECTORY:
		status = VfsQueryDirectory(irp_context, irp, stack_location, file_obj, fcb, ccb);
		break;
	case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
	//	status = VfsNotifyDirectory(irp_context, irp, stack_location, file_obj, fcb, ccb);
	//	break;
	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		LklCompleteRequest(irp, status);
		FreeIrpContext(irp_context);
		break;
	}

	return status;
}

