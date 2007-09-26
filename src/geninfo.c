/**
* querry/set information about files, volumes, etc.
* put all TODOs here:
* -set information
**/

#include <lklvfs.h>
#include <linux/magic.h>


#define SECTOR_SIZE 512
#define TEMP_FS_NAME "LKLVFS"
#define TEMP_FS_LENGTH 6


NTSTATUS DDKAPI VfsQueryVolumeInformation(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PLKLVCB vcb = NULL;
	PIO_STACK_LOCATION stack_location = NULL;
	FS_INFORMATION_CLASS FsInformationClass;
	ULONG Length;
	PVOID SystemBuffer;
	BOOLEAN VcbResourceAcquired = FALSE;
	BOOLEAN top_level;
	LONG rc;
	UINT fs_length;
    STATFS mystat;

	FsRtlEnterFileSystem();

	top_level = LklIsIrpTopLevel(irp);

	vcb = (PLKLVCB)device->DeviceExtension;
	CHECK_OUT(vcb == NULL, STATUS_INVALID_PARAMETER);
	CHECK_OUT(!ExAcquireResourceSharedLite(&vcb->vcb_resource, TRUE),STATUS_PENDING);
		VcbResourceAcquired = TRUE;

	stack_location = IoGetCurrentIrpStackLocation(irp);
	FsInformationClass = stack_location->Parameters.QueryVolume.FsInformationClass;
	Length = stack_location->Parameters.QueryVolume.Length;
	SystemBuffer = irp->AssociatedIrp.SystemBuffer;
	RtlZeroMemory(SystemBuffer, Length);
	rc = sys_statfs_wrapper(vcb->linux_device.mnt, &mystat);
	switch (FsInformationClass)
    {
		case FileFsVolumeInformation:
		{
			PFILE_FS_VOLUME_INFORMATION Buffer;
			ULONG VolumeLabelLength;
			ULONG RequiredLength;

			CHECK_OUT(Length < sizeof(FILE_FS_VOLUME_INFORMATION),STATUS_INFO_LENGTH_MISMATCH);
			// fill required info
			Buffer = (PFILE_FS_VOLUME_INFORMATION) SystemBuffer;

			Buffer->VolumeCreationTime.QuadPart = 0;
			Buffer->VolumeSerialNumber = vcb->vpb->SerialNumber;
			VolumeLabelLength = vcb->vpb->VolumeLabelLength;
			Buffer->VolumeLabelLength = VolumeLabelLength * sizeof(WCHAR);
			Buffer->SupportsObjects = FALSE;
			RequiredLength = sizeof(FILE_FS_VOLUME_INFORMATION)
				+ VolumeLabelLength * sizeof(WCHAR) - sizeof(WCHAR);

			if (Length < RequiredLength) {
				irp->IoStatus.Information = sizeof(FILE_FS_VOLUME_INFORMATION);
				status = STATUS_BUFFER_OVERFLOW;
			//	DbgPrint("FileFsVolumeInformation");
				TRY_RETURN(status);
			}

			RtlCopyMemory(Buffer->VolumeLabel, vcb->vpb->VolumeLabel,vcb->vpb->VolumeLabelLength);

			irp->IoStatus.Information = RequiredLength;
			status = STATUS_SUCCESS;
			TRY_RETURN(status);
		}
		 case FileFsSizeInformation:
		{
			PFILE_FS_SIZE_INFORMATION Buffer;

			CHECK_OUT(Length < sizeof(FILE_FS_SIZE_INFORMATION),
				STATUS_INFO_LENGTH_MISMATCH);

			Buffer = (PFILE_FS_SIZE_INFORMATION) SystemBuffer;

			Buffer->TotalAllocationUnits.QuadPart = mystat.f_blocks;
			Buffer->AvailableAllocationUnits.QuadPart = mystat.f_bavail;
			Buffer->SectorsPerAllocationUnit = mystat.f_bsize/SECTOR_SIZE;
			Buffer->BytesPerSector = SECTOR_SIZE;
			irp->IoStatus.Information = sizeof(FILE_FS_SIZE_INFORMATION);
			status = STATUS_SUCCESS;
		//	DbgPrint("FileFsSizeInformation");
			TRY_RETURN(status);
		}
		case FileFsDeviceInformation:
		{
			PFILE_FS_DEVICE_INFORMATION Buffer;

			CHECK_OUT(Length < sizeof(FILE_FS_DEVICE_INFORMATION),
				STATUS_INFO_LENGTH_MISMATCH);

			Buffer = (PFILE_FS_DEVICE_INFORMATION) SystemBuffer;

			Buffer->DeviceType = vcb->target_device->DeviceType;
			Buffer->Characteristics = vcb->target_device->Characteristics;
			irp->IoStatus.Information = sizeof(FILE_FS_DEVICE_INFORMATION);
			status = STATUS_SUCCESS;
		//	DbgPrint("FileFsDeviceInformation");
			TRY_RETURN(status)
		}
		case FileFsAttributeInformation:
		{
			PFILE_FS_ATTRIBUTE_INFORMATION  Buffer;
			ULONG RequiredLength;

			CHECK_OUT(Length < sizeof(FILE_FS_ATTRIBUTE_INFORMATION),STATUS_INFO_LENGTH_MISMATCH);

			Buffer = (PFILE_FS_ATTRIBUTE_INFORMATION) SystemBuffer;

			Buffer->FileSystemAttributes = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES;
			Buffer->MaximumComponentNameLength = mystat.f_namelen;

				switch ( mystat.f_type) {
                   case EXT3_SUPER_MAGIC:
                  	    Buffer->FileSystemNameLength = 4 * sizeof(WCHAR);
                        CharToWchar(Buffer->FileSystemName, "ext3",4);
                        fs_length = 4;
                        break;
                   case REISERFS_SUPER_MAGIC:
                         Buffer->FileSystemNameLength = 8 * sizeof(WCHAR);
                         CharToWchar(Buffer->FileSystemName, "Reiserfs",8);
                         fs_length = 8;
                         break;
                   default:
                         Buffer->FileSystemNameLength = TEMP_FS_LENGTH * sizeof(WCHAR);
                         CharToWchar(Buffer->FileSystemName, TEMP_FS_NAME,TEMP_FS_LENGTH);
                         fs_length = TEMP_FS_LENGTH;
                         break;  
            }
            
			RequiredLength = sizeof(FILE_FS_ATTRIBUTE_INFORMATION) +
            fs_length  * sizeof(WCHAR) - sizeof(WCHAR);
			if (Length < RequiredLength)
			{
				irp->IoStatus.Information =
					sizeof(FILE_FS_ATTRIBUTE_INFORMATION);
				status = STATUS_BUFFER_OVERFLOW;
		//		DbgPrint("FileFsAttributeInformation");
			TRY_RETURN(status);
			}
		
			irp->IoStatus.Information = RequiredLength;
			status = STATUS_SUCCESS;
			TRY_RETURN(status);
		}
		case FileFsFullSizeInformation:
        {
            PFILE_FS_FULL_SIZE_INFORMATION Buffer;

            CHECK_OUT(Length < sizeof(FILE_FS_FULL_SIZE_INFORMATION),
				STATUS_INFO_LENGTH_MISMATCH);

            Buffer = (PFILE_FS_FULL_SIZE_INFORMATION) SystemBuffer;

            Buffer->TotalAllocationUnits.QuadPart = mystat.f_blocks;
            Buffer->CallerAvailableAllocationUnits.QuadPart =
            Buffer->ActualAvailableAllocationUnits.QuadPart = mystat.f_bavail;
            Buffer->SectorsPerAllocationUnit = mystat.f_bsize/SECTOR_SIZE;
            Buffer->BytesPerSector = SECTOR_SIZE;
            irp->IoStatus.Information = sizeof(FILE_FS_FULL_SIZE_INFORMATION);
            status = STATUS_SUCCESS;
		//	DbgPrint("FileFsFullSizeInformation");
            TRY_RETURN(status);
		}
	default:
        status = STATUS_INVALID_INFO_CLASS;
    }
try_exit:

	if (VcbResourceAcquired) {
		RELEASE(&vcb->vcb_resource);
	}
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp,
		(CCHAR)(NT_SUCCESS(status) ? IO_DISK_INCREMENT : IO_NO_INCREMENT));

if (top_level)
	IoSetTopLevelIrp(NULL);

FsRtlExitFileSystem();

	return status;
}

NTSTATUS DDKAPI VfsQueryInformation(PDEVICE_OBJECT device ,PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	BOOLEAN top_level;
	BOOLEAN fcbResourceAcquired = FALSE;
	PIO_STACK_LOCATION	stack_location;
	PFILE_OBJECT file = NULL;
	PLKLFCB fcb = NULL;
	PLKLCCB ccb = NULL;
	PLKLVCB vcb = NULL;
	PSTR name;
	FILE_INFORMATION_CLASS file_info;
	ULONG length;
	LONG rc;
	PVOID buffer;
	STATS mystat;

	FsRtlEnterFileSystem();

	top_level = LklIsIrpTopLevel(irp);

    vcb = (PLKLVCB) device->DeviceExtension;
   	CHECK_OUT(vcb == NULL, STATUS_INVALID_PARAMETER);
    CHECK_OUT(vcb->id.type != VCB || vcb->id.size != sizeof(LKLVCB), STATUS_INVALID_PARAMETER);
	stack_location = IoGetCurrentIrpStackLocation(irp);
	file = stack_location->FileObject;
	CHECK_OUT(file == NULL, STATUS_INVALID_PARAMETER);
	fcb = (PLKLFCB) file->FsContext;
	CHECK_OUT(fcb == NULL, STATUS_INVALID_PARAMETER);

	if (!FLAG_ON(fcb->flags, VFS_FCB_PAGE_FILE)) {
		CHECK_OUT(!ExAcquireResourceSharedLite(&fcb->fcb_resource, TRUE), STATUS_PENDING) {
		}
		
		fcbResourceAcquired = TRUE;
	}

	ccb = (PLKLCCB) file->FsContext2;
	CHECK_OUT(ccb == NULL, STATUS_INVALID_PARAMETER);

	file_info = stack_location->Parameters.QueryFile.FileInformationClass;
	length = stack_location->Parameters.QueryFile.Length;
	buffer = irp->AssociatedIrp.SystemBuffer;
	name = VfsCopyUnicodeStringToZcharUnixPath(vcb->linux_device.mnt, 
         vcb->linux_device.mnt_length, &fcb->name,NULL, 0);
	DbgPrint("Query information on file: %s", name);
	rc = sys_newfstat_wrapper(ccb->fd, &mystat);
    ExFreePool(name);
    CHECK_OUT(rc<0, STATUS_INVALID_PARAMETER);
    
	switch (file_info) {

	case FileBasicInformation:
		{
		PFILE_BASIC_INFORMATION basic_buffer;
		CHECK_OUT(length < sizeof(FILE_BASIC_INFORMATION),
				STATUS_INFO_LENGTH_MISMATCH);
		basic_buffer = (PFILE_BASIC_INFORMATION) buffer;
		
		RtlSecondsSince1970ToTime (mystat.st_ctime, &basic_buffer->CreationTime);
		RtlSecondsSince1970ToTime (mystat.st_atime, &basic_buffer->LastAccessTime);
		RtlSecondsSince1970ToTime (mystat.st_mtime, &basic_buffer->LastWriteTime);
		RtlSecondsSince1970ToTime (mystat.st_mtime, &basic_buffer->ChangeTime);
		basic_buffer->FileAttributes = FILE_ATTRIBUTE_NORMAL;
		if (FLAG_ON(fcb->flags, VFS_FCB_DIRECTORY)) 
			SET_FLAG(basic_buffer->FileAttributes, FILE_ATTRIBUTE_DIRECTORY);
		irp->IoStatus.Information = sizeof(FILE_BASIC_INFORMATION);
		
		DbgPrint("File basic");
		TRY_RETURN(STATUS_SUCCESS);
		}
	case FileStandardInformation:
         {
	     PFILE_STANDARD_INFORMATION	st_buffer;
         CHECK_OUT(length < sizeof(FILE_STANDARD_INFORMATION),
				STATUS_INFO_LENGTH_MISMATCH);
				
		st_buffer = (PFILE_STANDARD_INFORMATION) buffer;
		st_buffer->AllocationSize.QuadPart = mystat.st_blksize * mystat.st_blocks;
		st_buffer->EndOfFile.QuadPart = mystat.st_size;
		st_buffer->NumberOfLinks = mystat.st_nlink;
		st_buffer->DeletePending = FLAG_ON(fcb->flags, VFS_FCB_DELETE_ON_CLOSE);
		st_buffer->Directory = FLAG_ON(fcb->flags, VFS_FCB_DIRECTORY);
		irp->IoStatus.Information = sizeof(FILE_STANDARD_INFORMATION);
		
		DbgPrint("File standard information");
		TRY_RETURN(STATUS_SUCCESS); 
    }
    case FileNetworkOpenInformation:
         {
        PFILE_NETWORK_OPEN_INFORMATION nt_buffer;
        CHECK_OUT(length < sizeof(FILE_STANDARD_INFORMATION),
			STATUS_INFO_LENGTH_MISMATCH);
			
	    nt_buffer = (PFILE_NETWORK_OPEN_INFORMATION) buffer;
     	RtlSecondsSince1970ToTime (mystat.st_ctime, &nt_buffer->CreationTime);
	    RtlSecondsSince1970ToTime (mystat.st_atime, &nt_buffer->LastAccessTime);
	    RtlSecondsSince1970ToTime (mystat.st_mtime, &nt_buffer->LastWriteTime);
	    RtlSecondsSince1970ToTime (mystat.st_mtime, &nt_buffer->ChangeTime);
	    
	    nt_buffer->FileAttributes = FILE_ATTRIBUTE_NORMAL;
	    if (FLAG_ON(fcb->flags, VFS_FCB_DIRECTORY)) 
		   SET_FLAG(nt_buffer->FileAttributes, FILE_ATTRIBUTE_DIRECTORY);
		nt_buffer->AllocationSize.QuadPart = mystat.st_blksize * mystat.st_blocks;
	    nt_buffer->EndOfFile.QuadPart = mystat.st_size; 
        irp->IoStatus.Information = sizeof(FILE_NETWORK_OPEN_INFORMATION);
        
		DbgPrint("File network open information");
	    TRY_RETURN(STATUS_SUCCESS);  
         }
	case FileInternalInformation:
		{
		PFILE_INTERNAL_INFORMATION in_buffer;
		CHECK_OUT(length < sizeof(FILE_INTERNAL_INFORMATION),
		STATUS_INFO_LENGTH_MISMATCH);
		
        DbgPrint("File internal information");
		in_buffer = (PFILE_INTERNAL_INFORMATION) buffer;
		in_buffer->IndexNumber.QuadPart = fcb->ino; 
		irp->IoStatus.Information = sizeof(FILE_INTERNAL_INFORMATION);
		
		TRY_RETURN(STATUS_SUCCESS);
		}
	case FileEaInformation:
		{
		PFILE_EA_INFORMATION ea_buffer;
		CHECK_OUT(length < sizeof(FILE_EA_INFORMATION),
				STATUS_INFO_LENGTH_MISMATCH);

		ea_buffer = (PFILE_EA_INFORMATION) buffer;
		ea_buffer->EaSize = 0;
		irp->IoStatus.Information = sizeof(FILE_EA_INFORMATION);
		
		TRY_RETURN(STATUS_SUCCESS);
		}
	case FileNameInformation:
		{
		PFILE_NAME_INFORMATION name_buffer;
		CHECK_OUT(length < sizeof(FILE_NAME_INFORMATION) +
                fcb->name.Length - sizeof(WCHAR),
				STATUS_INFO_LENGTH_MISMATCH);
				
		DbgPrint("File name");
		name_buffer = (PFILE_NAME_INFORMATION) buffer;
		name_buffer->FileNameLength = fcb->name.Length;
		RtlCopyMemory(name_buffer->FileName, fcb->name.Buffer, fcb->name.Length);
		irp->IoStatus.Information = sizeof(FILE_NAME_INFORMATION) + fcb->name.Length - sizeof(WCHAR);
                
		TRY_RETURN(STATUS_SUCCESS);
		}
	case FileAttributeTagInformation:
		{
		PFILE_ATTRIBUTE_TAG_INFORMATION	atr_buffer;
		
		DbgPrint("File atr tag");
		atr_buffer = (PFILE_ATTRIBUTE_TAG_INFORMATION) buffer;
		atr_buffer->FileAttributes = FILE_ATTRIBUTE_NORMAL;
		if (FLAG_ON(fcb->flags, VFS_FCB_DIRECTORY)) {
			SET_FLAG(atr_buffer->FileAttributes, FILE_ATTRIBUTE_DIRECTORY);
		}
		atr_buffer->ReparseTag = 0; 
		irp->IoStatus.Information = sizeof(FILE_ATTRIBUTE_TAG_INFORMATION);
		
		TRY_RETURN(STATUS_SUCCESS);
		}
	case FilePositionInformation:
		{

        PFILE_POSITION_INFORMATION pos_buffer;
        CHECK_OUT(length < sizeof(FILE_POSITION_INFORMATION),
			STATUS_INFO_LENGTH_MISMATCH);
			
         DbgPrint("File position info");
        pos_buffer = (PFILE_POSITION_INFORMATION) buffer;
		pos_buffer->CurrentByteOffset = file->CurrentByteOffset;
        irp->IoStatus.Information = sizeof(FILE_POSITION_INFORMATION);
        
		TRY_RETURN(STATUS_SUCCESS);
		}
	case FileAllInformation:
		{
		PFILE_ALL_INFORMATION     all_buffer;
		PFILE_BASIC_INFORMATION     basic_info;
		PFILE_STANDARD_INFORMATION  standard_info;
		PFILE_INTERNAL_INFORMATION  int_info;
		PFILE_EA_INFORMATION        ea_info;
		PFILE_POSITION_INFORMATION  pos_info;
		PFILE_NAME_INFORMATION      name_info;

		CHECK_OUT(length<sizeof(FILE_ALL_INFORMATION), STATUS_INFO_LENGTH_MISMATCH);
        DbgPrint("File all info class");
		all_buffer = (PFILE_ALL_INFORMATION) buffer;
		basic_info = &all_buffer->BasicInformation;
		standard_info = &all_buffer->StandardInformation;
		int_info = &all_buffer->InternalInformation;
		ea_info = &all_buffer->EaInformation;
		pos_info = &all_buffer->PositionInformation;
		name_info = &all_buffer->NameInformation;

		// now fill the info
		ea_info->EaSize = 0;
		pos_info->CurrentByteOffset = file->CurrentByteOffset;
		int_info->IndexNumber.QuadPart = fcb->ino;
		RtlSecondsSince1970ToTime (mystat.st_ctime, & basic_info->CreationTime);
		RtlSecondsSince1970ToTime (mystat.st_atime, & basic_info->LastAccessTime);
		RtlSecondsSince1970ToTime (mystat.st_mtime, & basic_info->LastWriteTime);
		RtlSecondsSince1970ToTime (mystat.st_mtime, & basic_info->ChangeTime);
		standard_info->AllocationSize.QuadPart = mystat.st_blksize * mystat.st_blocks;
		standard_info->EndOfFile.QuadPart = mystat.st_size;
		standard_info->NumberOfLinks = mystat.st_nlink;
		standard_info->DeletePending = FLAG_ON(fcb->flags, VFS_FCB_DELETE_ON_CLOSE);
		standard_info->Directory = FLAG_ON(fcb->flags, VFS_FCB_DIRECTORY);
		if (length < sizeof(FILE_ALL_INFORMATION) + fcb->name.Length - sizeof(WCHAR))
            {
                irp->IoStatus.Information = sizeof(FILE_ALL_INFORMATION);
                status = STATUS_BUFFER_OVERFLOW;
                TRY_RETURN(status);
            }

        name_info->FileNameLength = fcb->name.Length;
        RtlCopyMemory(name_info->FileName, fcb->name.Buffer, fcb->name.Length);

        irp->IoStatus.Information = sizeof(FILE_ALL_INFORMATION) +
						fcb->name.Length - sizeof(WCHAR);

	    TRY_RETURN(STATUS_SUCCESS);
		}
	default:
        DbgPrint("Invalid info class");
		status = STATUS_INVALID_INFO_CLASS;
	}
	
try_exit:
	if(fcbResourceAcquired)
		RELEASE(&fcb->fcb_resource);
		
    ASSERT(status!=STATUS_PENDING);
    
    LklCompleteRequest(irp, status);

    if (top_level)
    	IoSetTopLevelIrp(NULL);
    
    FsRtlExitFileSystem();

    return status;
}


NTSTATUS DDKAPI VfsSetInformation(PDEVICE_OBJECT device ,PIRP irp)
{
	NTSTATUS	            status = STATUS_UNSUCCESSFUL;
	PFILE_OBJECT	        FileObject;
	PLKLFCB                 Fcb;
	PLKLCCB                 Ccb;
	PIO_STACK_LOCATION	    IrpSp;
	FILE_INFORMATION_CLASS	FileInformationClass;
	ULONG		            Length;
	PVOID		            SystemBuffer;
	BOOLEAN		            FcbResourceAcquired = FALSE;
	BOOLEAN                 top_level = FALSE;

	FsRtlEnterFileSystem();

	top_level = LklIsIrpTopLevel(irp);

	IrpSp = IoGetCurrentIrpStackLocation(irp);
	FileObject = IrpSp->FileObject;
	Fcb = (PLKLFCB) FileObject->FsContext;
	CHECK_OUT(!Fcb, STATUS_INVALID_PARAMETER);

	if (!FLAG_ON(Fcb->flags, VFS_FCB_PAGE_FILE))
	{
		if(!ExAcquireResourceSharedLite(&Fcb->fcb_resource, TRUE)) {
			status = STATUS_PENDING;
			goto try_exit;
		}
		
		FcbResourceAcquired = TRUE;
	}

	Ccb = (PLKLCCB)FileObject->FsContext2;
	CHECK_OUT(!Ccb, STATUS_INVALID_PARAMETER);

	FileInformationClass = IrpSp->Parameters.QueryFile.FileInformationClass;
	Length = IrpSp->Parameters.QueryFile.Length;
    SystemBuffer = irp->AssociatedIrp.SystemBuffer;
	RtlZeroMemory(SystemBuffer, Length);

	switch(FileInformationClass){
                                 
	 case FilePositionInformation:
       {
         PFILE_POSITION_INFORMATION Buffer = (PFILE_POSITION_INFORMATION) SystemBuffer;
         CHECK_OUT(Length < sizeof(FILE_POSITION_INFORMATION), STATUS_INFO_LENGTH_MISMATCH);
         
		 sys_lseek_wrapper(Ccb->fd, Buffer->CurrentByteOffset.QuadPart, 0); 
		 
		 irp->IoStatus.Information = sizeof(FILE_POSITION_INFORMATION);
		 TRY_RETURN(STATUS_SUCCESS);
      }
    case FileBasicInformation:
     {
        PFILE_BASIC_INFORMATION Buffer = (PFILE_BASIC_INFORMATION) SystemBuffer;
        CHECK_OUT(Length < sizeof(FILE_BASIC_INFORMATION), STATUS_INFO_LENGTH_MISMATCH);
        
	    // do nothing for now, because we mounted a read-only fs
	    
	    irp->IoStatus.Information = sizeof(FILE_BASIC_INFORMATION);
        TRY_RETURN(STATUS_SUCCESS);
     }
	default:
		status = STATUS_INVALID_INFO_CLASS;
	}

try_exit:
		
	if (FcbResourceAcquired)
		RELEASE(&Fcb->fcb_resource);
		
	ASSERT(status != STATUS_PENDING);

	LklCompleteRequest(irp, status);

	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;
}
