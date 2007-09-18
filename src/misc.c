/**
* useful functions
* TODOs: uncomment lines!
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

void LklCompleteRequest(PIRP irp, NTSTATUS status)
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

void VfsReportError(const char * string)
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

void linux_kernel_thread(PVOID p)
{
	/*NTSTATUS status=STATUS_SUCCESS;
	struct linux_native_operations lnops;
	RtlZeroMemory(&lnops, sizeof(struct linux_native_operations));
	lnops.panic_blink=linux_panic_blink;
	lnops.mem_init=linux_mem_init;
	lnops.main=linux_main;
	threads_init(&lnops);

	__try
	{
		DbgPrint("Start linux kernel");
		linux_start_kernel(&lnops, "root=%d:0", FILE_DISK_MAJOR);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
		{
			status = GetExceptionCode();
		}
		if(!NT_SUCCESS(status))
			DbgPrint("Exception %x in starting linux kernel", status);
	*/
}

void unload_linux_kernel()
{
	//TODO - cleanup the mess

	// and finally...
	//ZwClose(lklfsd.linux_thread);
}

NTSTATUS run_linux_kernel()
{
	NTSTATUS status=STATUS_SUCCESS;
	//status = PsCreateSystemThread(&lklfsd.linux_thread, (ACCESS_MASK)0L,
	//			NULL, NULL, NULL, &linux_kernel_thread, NULL);

	return status;

}
