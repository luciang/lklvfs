/**
* usefull functions
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
		if (NT_ERROR(status) && FLAG_ON(irp->Flags, IRP_INPUT_OPERATION))
			irp->IoStatus.Information = 0;
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_DISK_INCREMENT);
	}
	return;
}

NTSTATUS LklDummyIrp(PDEVICE_OBJECT dev_obj, PIRP irp)
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