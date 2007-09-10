/**
* querry/set information about files, volumes, etc.
**/

#include <lklvfs.h>

#define SECTOR_SIZE 512
#define TEMP_FS_NAME "LKLVFS"
#define TEMP_FS_LENGTH 3

NTSTATUS LklQueryVolumeInformation(PDEVICE_OBJECT device, PIRP irp)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PLKLVCB vcb = NULL;
	PIO_STACK_LOCATION IrpSp = NULL;
	FS_INFORMATION_CLASS FsInformationClass;
	ULONG Length;
	PVOID SystemBuffer;
	BOOLEAN VcbResourceAcquired = FALSE;
	BOOLEAN top_level;

	DbgPrint("Querry Volume Information");
	FsRtlEnterFileSystem();

	top_level = LklIsIrpTopLevel(irp);

	__try {
		vcb = (PLKLVCB)device->DeviceExtension;
		ASSERT(vcb);
		CHECK_OUT(!ExAcquireResourceSharedLite(&vcb->vcb_resource, TRUE),STATUS_PENDING);
			VcbResourceAcquired = TRUE;

		IrpSp = IoGetCurrentIrpStackLocation(irp);
		FsInformationClass = IrpSp->Parameters.QueryVolume.FsInformationClass;
		Length = IrpSp->Parameters.QueryVolume.Length;
		SystemBuffer = irp->AssociatedIrp.SystemBuffer;
		RtlZeroMemory(SystemBuffer, Length);
		// TODO -- make a statfs here!
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
						__leave;
					}

					RtlCopyMemory(Buffer->VolumeLabel, vcb->vpb->VolumeLabel,vcb->vpb->VolumeLabelLength);

					irp->IoStatus.Information = RequiredLength;
					status = STATUS_SUCCESS;
					__leave;
				}
			 case FileFsSizeInformation:
				{
					PFILE_FS_SIZE_INFORMATION Buffer;

					CHECK_OUT(Length < sizeof(FILE_FS_SIZE_INFORMATION),
						STATUS_INFO_LENGTH_MISMATCH);

					Buffer = (PFILE_FS_SIZE_INFORMATION) SystemBuffer;
					// TODO --fix all this
					Buffer->TotalAllocationUnits.QuadPart = 0;
					Buffer->AvailableAllocationUnits.QuadPart = 0;
					Buffer->SectorsPerAllocationUnit = 1;
					Buffer->BytesPerSector = SECTOR_SIZE;
					irp->IoStatus.Information = sizeof(FILE_FS_SIZE_INFORMATION);
					status = STATUS_SUCCESS;
					DbgPrint("VOLUME INFORMATION SIZE");
					__leave;
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
					DbgPrint("DEVICE INFORMATION");
					__leave;
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
						DbgPrint("FileFsAttributeInformation\n");
						__leave;
					}
					CharToWchar(Buffer->FileSystemName, TEMP_FS_NAME,TEMP_FS_LENGTH);
					irp->IoStatus.Information = RequiredLength;
					status = STATUS_SUCCESS;
					__leave;
				}
		default:
            status = STATUS_INVALID_INFO_CLASS;
        }
		try_exit:
	;
	}
	__finally {
		if (VcbResourceAcquired) {
			RELEASE(&vcb->vcb_resource);
		}
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp,
			(CCHAR)(NT_SUCCESS(status) ? IO_DISK_INCREMENT : IO_NO_INCREMENT));
	}
	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;
}
