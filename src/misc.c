/**
* useful functions
**/

#include<lklvfs.h>

BOOLEAN LklIsIrpTopLevel(PIRP irp)
{
	if (IoGetTopLevelIrp() == NULL) {
		IoSetTopLevelIrp(irp);
		return TRUE;
	}
	return FALSE;
}

VOID LklCompleteRequest(PIRP irp, NTSTATUS status)
{
	if (irp != NULL) {
		if (!NT_SUCCESS(status) && FLAG_ON(irp->Flags, IRP_INPUT_OPERATION))
			irp->IoStatus.Information = 0;
		irp->IoStatus.Status = status;

		IoCompleteRequest(irp, NT_SUCCESS(status) ? IO_DISK_INCREMENT : IO_NO_INCREMENT);
	}
	return;
}


NTSTATUS DDKAPI VfsDummyIrp(PDEVICE_OBJECT dev_obj, PIRP irp)
{
	BOOLEAN top_level = FALSE;
	NTSTATUS status = STATUS_NOT_IMPLEMENTED;

	DbgPrint("****** DUMMY IRP Handler *******");

	FsRtlEnterFileSystem();

	top_level = LklIsIrpTopLevel(irp);

	irp->IoStatus.Status = status;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_DISK_INCREMENT);

	if (top_level)
		IoSetTopLevelIrp(NULL);

	FsRtlExitFileSystem();

	return status;
}

VOID CharToWchar(PWCHAR Destination, PCHAR Source, ULONG Length)
{
	ULONG	Index;

	ASSERT(Destination != NULL);
	ASSERT(Source != NULL);

	for (Index = 0; Index < Length; Index++) {
		Destination[Index] = (WCHAR)Source[Index];
	}
}

PSTR VfsCopyUnicodeStringToZcharUnixPath(PSTR root_path, USHORT root_path_len,
      PUNICODE_STRING src, PSTR rel_name, USHORT name_length)
{
	int i, length;
	PSTR dest;
    
    if(rel_name == NULL )
        name_length = 0;
        
	length = src->Length / sizeof(WCHAR);
	dest = ExAllocateFromNPagedLookasideList(name_cachep);

	if (!dest)
		return NULL;
		
	RtlZeroMemory(dest, STR_MAX_LEN);

	for(i = 0; i < root_path_len; i++) {
          dest[i] = (char) root_path[i];
    }
    
	for (i = 0; i < length; i++) {
		dest[root_path_len + i] = (char)src->Buffer[i];
		if (dest[root_path_len + i] == '\\') dest[root_path_len + i] = '/';
	}
	
	root_path_len = root_path_len + length;
	
	if(dest[root_path_len-1] != '/') {
	  dest[root_path_len] = '/';
	  root_path_len++;
   }
    for(i = 0; i < name_length; i++) {
          dest[root_path_len+i] = (char) rel_name[i];
    }
    dest[root_path_len + name_length] = 0;
    
	return dest;
}

PSTR CopyAppendUStringToZcharUnixPath(PUNICODE_STRING src, PSTR rel_name, USHORT name_length)
{
     int i, length;
     PSTR dest;
     
     length = src->Length / sizeof(WCHAR);
     dest = ExAllocatePoolWithTag(NonPagedPool, length + name_length + 1, 'RHCU');
     if(!dest)
      return NULL;
     for (i = 0; i < length; i++) {
		dest[i] = (char)src->Buffer[i];
		if (dest[i] == '\\') dest[i] = '/';
	}
	if(dest[length-1] != '/') {
	  dest[length] = '/';
	  length++;
   }
    for(i = 0; i < name_length; i++) {
          dest[length+i] = (char) rel_name[i];
    }
    dest[length+name_length] = 0;
    
    return dest;
}

void FreeUnixPathString(PSTR name)
{
     ExFreeToNPagedLookasideList(name_cachep, name);
}
// for device name
PSTR CopyStringAppendULong(PSTR src, USHORT src_length, ULONG number)
{
    PSTR dest;
    int i, letter;
    dest = ExAllocatePoolWithTag(NonPagedPool, STR_MAX_LEN,'RAHC');
    if(!dest)
        return NULL;
    RtlZeroMemory(dest, STR_MAX_LEN);
    
	for(i = 0; i < src_length; i++) {
      dest[i] = (char) src[i];
      }
    while(number>0) {
         letter = number % 10;
         dest[i++] = (char) ('0' + letter);
         number = number /10;
    }
    
    return dest;
}

void VfsCopyUnicodeString(PUNICODE_STRING dest, PUNICODE_STRING src)
{
	
	dest->Length = src->Length;
	dest->MaximumLength = src->MaximumLength = src->Length+2;
	dest->Buffer = ExAllocatePoolWithTag(NonPagedPool, dest->MaximumLength, 'RAHC');
    if(dest->Buffer == NULL)
       return;
	RtlCopyUnicodeString(dest, src);
}

VOID VfsReportError(const char * string)
{
	//todo - make a nice log entry (but for now we stick with DbgPrint)

	DbgPrint("****%s****",string);
}

// will need this for reading the partition table and find out the partition size
NTSTATUS BlockDeviceIoControl(IN PDEVICE_OBJECT DeviceObject, IN ULONG	IoctlCode,
								   IN PVOID	InputBuffer, IN ULONG InputBufferSize, 
								   IN OUT PVOID OutputBuffer, IN OUT PULONG OutputBufferSize)
{
	ULONG			OutputBufferSize2 = 0;
	KEVENT			Event;
	PIRP			Irp;
	IO_STATUS_BLOCK	IoStatus;
	NTSTATUS		Status;

	ASSERT(DeviceObject != NULL);

	if (OutputBufferSize)
	{
		OutputBufferSize2 = *OutputBufferSize;
	}

	KeInitializeEvent(&Event, NotificationEvent, FALSE);

	Irp = IoBuildDeviceIoControlRequest(IoctlCode, DeviceObject, InputBuffer, InputBufferSize,
		OutputBuffer, OutputBufferSize2, FALSE, &Event, &IoStatus);
	if (!Irp) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	Status = IoCallDriver(DeviceObject, Irp);
	if (Status == STATUS_PENDING)
	{
		KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
		Status = IoStatus.Status;
	}
	if (OutputBufferSize)
	{
		*OutputBufferSize = (ULONG) IoStatus.Information;
	}

	return Status;
}

PVOID GetUserBuffer(IN PIRP Irp)
{
    if (Irp->MdlAddress) {
        return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    } else {
        return Irp->UserBuffer;
    }
}

NTSTATUS LockUserBuffer(IN PIRP Irp, IN ULONG Length, IN LOCK_OPERATION Operation)
{
    NTSTATUS Status;

    ASSERT(Irp != NULL);

    if (Irp->MdlAddress != NULL) {
        return STATUS_SUCCESS;
    }
    IoAllocateMdl(Irp->UserBuffer, Length, FALSE, FALSE, Irp);
    if (Irp->MdlAddress == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

//    try
  //  {
        MmProbeAndLockPages(Irp->MdlAddress, Irp->RequestorMode, Operation);
        Status = STATUS_SUCCESS;
    //}
   /// except (EXCEPTION_EXECUTE_HANDLER)
    //{
      //  IoFreeMdl(Irp->MdlAddress);
       // Irp->MdlAddress = NULL;
       // Status = STATUS_INVALID_USER_BUFFER;
    //}
    return Status;
}



