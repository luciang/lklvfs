/**
* querry/set information about files, volumes, etc.
* put all TODOs here:
* -uncomment lines related to stat & review statfs & fstat
**/

#include <lklvfs.h>

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
	//ULONG rc;
	//struct statfs mystat;

	DbgPrint("Querry Volume Information");
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
	// TODO -- make a statfs here! rc = sys_statfs(vcb->volume_path, &mystat);
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
				DbgPrint("FileFsVolumeInformation");
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
			// TODO --fix all this
			Buffer->TotalAllocationUnits.QuadPart = vcb->partition_information.PartitionLength.QuadPart; // / mystat.f_bsize
			Buffer->AvailableAllocationUnits.QuadPart = 0;// mystat.f_bavail
			Buffer->SectorsPerAllocationUnit = 1; //mystat.f_bsize/SECTOR_SIZE
			Buffer->BytesPerSector = SECTOR_SIZE;
			irp->IoStatus.Information = sizeof(FILE_FS_SIZE_INFORMATION);
			status = STATUS_SUCCESS;
			DbgPrint("FileFsSizeInformation");
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
			DbgPrint("FileFsDeviceInformation");
			TRY_RETURN(status)
		}
		case FileFsAttributeInformation:
		{
			PFILE_FS_ATTRIBUTE_INFORMATION  Buffer;
			ULONG RequiredLength;

			CHECK_OUT(Length < sizeof(FILE_FS_ATTRIBUTE_INFORMATION),STATUS_INFO_LENGTH_MISMATCH);

			Buffer = (PFILE_FS_ATTRIBUTE_INFORMATION) SystemBuffer;

			Buffer->FileSystemAttributes = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES;
			Buffer->MaximumComponentNameLength = 255;
			// TODO -- get the real file system name and fill in the info
			Buffer->FileSystemNameLength = TEMP_FS_LENGTH * sizeof(WCHAR);
			RequiredLength = sizeof(FILE_FS_ATTRIBUTE_INFORMATION) +
			   TEMP_FS_LENGTH  * sizeof(WCHAR) - sizeof(WCHAR);
			if (Length < RequiredLength)
			{
				irp->IoStatus.Information =
					sizeof(FILE_FS_ATTRIBUTE_INFORMATION);
				status = STATUS_BUFFER_OVERFLOW;
				DbgPrint("FileFsAttributeInformation");
			TRY_RETURN(status);
			}
			CharToWchar(Buffer->FileSystemName, TEMP_FS_NAME,TEMP_FS_LENGTH);
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

            Buffer->TotalAllocationUnits.QuadPart = vcb->partition_information.PartitionLength.QuadPart; // / mystat.f_bsize;
            Buffer->CallerAvailableAllocationUnits.QuadPart =
            Buffer->ActualAvailableAllocationUnits.QuadPart = 0; //mystat.f_bavail

            Buffer->SectorsPerAllocationUnit = 1; // mystat.f_bsize / SECTOR_SIZE
            Buffer->BytesPerSector = SECTOR_SIZE;
            irp->IoStatus.Information = sizeof(FILE_FS_FULL_SIZE_INFORMATION);
            status = STATUS_SUCCESS;
			DbgPrint("FileFsFullSizeInformation");
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
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	BOOLEAN top_level;
	BOOLEAN fcbResourceAcquired = FALSE;
	PIO_STACK_LOCATION	stack_location;
	PFILE_OBJECT file = NULL;
	PLKLFCB fcb = NULL;
	PLKLCCB ccb = NULL;
	PLKLVCB vcb = NULL;
	FILE_INFORMATION_CLASS file_info;
	ULONG length;
	//ULONG rc;
	PVOID buffer;
	//struct stat mystat;

	DbgPrint("Querry Information");
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
		if(!ExAcquireResourceSharedLite(&fcb->fcb_resource, TRUE)) {
			status = STATUS_PENDING;
			TRY_RETURN(status);
		}
		
		fcbResourceAcquired = TRUE;
	}

	ccb = (PLKLCCB) file->FsContext2;
	CHECK_OUT(vcb == NULL, STATUS_INVALID_PARAMETER);

	file_info = stack_location->Parameters.QueryFile.FileInformationClass;
	length = stack_location->Parameters.QueryFile.Length;
	buffer = irp->AssociatedIrp.SystemBuffer;
	// stat on ccb->fd (statfd) rc = sys_newfstat(ccb->fd, &mystat);

	switch (file_info) {

	case FileBasicInformation:
		{
		/*PFILE_BASIC_INFORMATION basic_buffer;
		CHECK_OUT(length < sizeof(FILE_BASIC_INFORMATION),
				STATUS_INFO_LENGTH_MISMATCH);
		basic_buffer = (PFILE_BASIC_INFORMATION) buffer;
		RtlSecondsSince1970ToTime (mystat.st_ctime, & basic_buffer->CreationTime);
		RtlSecondsSince1970ToTime (mystat.st_atime, & basic_buffer->LastAccessTime);
		RtlSecondsSince1970ToTime (mystat.st_mtime, & basic_buffer->LastWriteTime);
		RtlSecondsSince1970ToTime (mystat.st_mtime, & basic_buffer->ChangeTime);
		basic_buffer->FileAttributes = FILE_ATTRIBUTE_NORMAL;
		if (S_ISDIR(mystat.st_mode)) 
			SET_FLAG(basic_buffer->FileAttributes, FILE_ATTRIBUTE_DIRECTORY);
		irp->IoStatus.Information = sizeof(FILE_BASIC_INFORMATION);*/
		TRY_RETURN(status);
		}
	case FileStandardInformation:
		/*PFILE_STANDARD_INFORMATION	st_buffer;
         CHECK_OUT(Length < sizeof(FILE_STANDARD_INFORMATION),
				STATUS_INFO_LENGTH_MISMATCH);
		st_buffer = (PFILE_STANDARD_INFORMATION) buffer;
		st_buffer->AllocationSize.QuadPart = mystat.st_size;
		st_buffer->EndOfFile.QuadPart = mystat.st_size;
		st_buffer->NumberOfLinks = mystat.st_nlink;
		st_buffer->DeletePending = FLAG_ON(fcb->flags, VFS_FCB_DELETE_ON_CLOSE);
		st_buffer->Directory = S_ISDIR(mystat.st_mode);
		irp->IoStatus.Information = sizeof(FILE_STANDARD_INFORMATION);*/
		TRY_RETURN(status);
	case FileInternalInformation:
		{
		PFILE_INTERNAL_INFORMATION in_buffer;
		CHECK_OUT(length < sizeof(FILE_INTERNAL_INFORMATION),
		STATUS_INFO_LENGTH_MISMATCH);
		in_buffer = (PFILE_INTERNAL_INFORMATION) buffer;
		in_buffer->IndexNumber.QuadPart = fcb->ino; 
		irp->IoStatus.Information = sizeof(FILE_INTERNAL_INFORMATION);
		TRY_RETURN(status);
		}
	case FileEaInformation:
		{
		PFILE_EA_INFORMATION ea_buffer;
		CHECK_OUT(length < sizeof(FILE_EA_INFORMATION),
				STATUS_INFO_LENGTH_MISMATCH);
		ea_buffer = (PFILE_EA_INFORMATION) buffer;
		ea_buffer->EaSize = 0;
		irp->IoStatus.Information = sizeof(FILE_EA_INFORMATION);
		TRY_RETURN(status);
		}
	case FileNameInformation:
		{
		PFILE_NAME_INFORMATION name_buffer;
		CHECK_OUT(length < sizeof(FILE_NAME_INFORMATION) +
                fcb->name.Length - sizeof(WCHAR),
				STATUS_INFO_LENGTH_MISMATCH);
		name_buffer = (PFILE_NAME_INFORMATION) buffer;
		name_buffer->FileNameLength = fcb->name.Length;
		RtlCopyMemory(name_buffer->FileName, fcb->name.Buffer, fcb->name.Length);
		irp->IoStatus.Information = sizeof(FILE_NAME_INFORMATION) +
                fcb->name.Length - sizeof(WCHAR);
		TRY_RETURN(status);
		}
	case FileAttributeTagInformation:
		{
		/*PFILE_ATTRIBUTE_TAG_INFORMATION	atr_buffer;
		atr_buffer = (PFILE_ATTRIBUTE_TAG_INFORMATION) buffer;
		atr_buffer->FileAttributes = FILE_ATTRIBUTE_NORMAL;
		if (S_ISDIR(mystat.st_mode)) {
			SET_FLAG(Buffer->FileAttributes, FILE_ATTRIBUTE_DIRECTORY);
		}
		Buffer->ReparseTag = 0; 
		irp->IoStatus.Information = sizeof(FILE_ATTRIBUTE_TAG_INFORMATION);*/
		TRY_RETURN(status);
		}
	case FilePositionInformation:
		{
        PFILE_POSITION_INFORMATION pos_buffer;
        CHECK_OUT(length < sizeof(FILE_POSITION_INFORMATION),
			STATUS_INFO_LENGTH_MISMATCH);
        pos_buffer = (PFILE_POSITION_INFORMATION) buffer;
		pos_buffer->CurrentByteOffset = file->CurrentByteOffset;
        irp->IoStatus.Information = sizeof(FILE_POSITION_INFORMATION);
		TRY_RETURN(status);
		}
	case FileAllInformation:
		{
		/*PFILE_ALL_INFORMATION     all_buffer;
		PFILE_BASIC_INFORMATION     basic_info;
		PFILE_STANDARD_INFORMATION  standard_info;
		PFILE_INTERNAL_INFORMATION  int_info;
		PFILE_EA_INFORMATION        ea_info;
		PFILE_POSITION_INFORMATION  pos_info;
		PFILE_NAME_INFORMATION      name_info;

		CHECK_OUT(length<sizeof(FILE_ALL_INFORMATION), STATUS_INFO_LENGTH_MISMATCH);

		all_buffer = (PFILE_ALL_INFORMATION) SystemBuffer;
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
		standard_info->AllocationSize.QuadPart = mystat.st_size;
		standard_info->EndOfFile.QuadPart = mystat.st_size;
		standard_info->NumberOfLinks = mystat.st_nlink;
		standard_info->DeletePending = FLAG_ON(fcb->flags, VFS_FCB_DELETE_ON_CLOSE);
		standard_info->Directory = S_ISDIR(mystat.st_mode);
		if (length < sizeof(FILE_ALL_INFORMATION) +
                fcb->name.Length - sizeof(WCHAR))
            {
                irp->IoStatus.Information = sizeof(FILE_ALL_INFORMATION);
                status = STATUS_BUFFER_OVERFLOW;
                TRY_RETURN(status);
            }

        name_info->FileNameLength = fcb->name.Length;
        RtlCopyMemory(name_info->FileName, fcb->name.Buffer, fcb->FileName.Length);

        irp->IoStatus.Information = sizeof(FILE_ALL_INFORMATION) +
						fcb->name.Length - sizeof(WCHAR);
		*/
	TRY_RETURN(status);
		}
	default:
		status = STATUS_INVALID_INFO_CLASS;
	}
try_exit:
	if(fcbResourceAcquired)
		RELEASE(&fcb->fcb_resource);
	LklCompleteRequest(irp, status);

    if (top_level)
    	IoSetTopLevelIrp(NULL);
    
    FsRtlExitFileSystem();

    return status;
}


NTSTATUS DDKAPI VfsSetInformation(PDEVICE_OBJECT device ,PIRP irp)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    BOOLEAN top_level;
    
    DbgPrint("Set Information");
    FsRtlEnterFileSystem();
    
    top_level = LklIsIrpTopLevel(irp);
    
    //TODO
    
    LklCompleteRequest(irp, status);
    
    if (top_level)
    	IoSetTopLevelIrp(NULL);
    
    FsRtlExitFileSystem();

	return status;
}
