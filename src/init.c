/**
* driver initialization functions
**/

#include <lklvfs.h>
#include<fastio.h>


LKLFSD lklfsd;
PNPAGED_LOOKASIDE_LIST ccb_cachep;
PNPAGED_LOOKASIDE_LIST fcb_cachep;
PNPAGED_LOOKASIDE_LIST irp_context_cachep;

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg_path)
{
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING device_name;
	UNICODE_STRING dos_name;
	BOOLEAN resource_init = FALSE;

	ccb_cachep = NULL;
	fcb_cachep = NULL;
	irp_context_cachep = NULL;

	__try {
		__try {
			RtlZeroMemory(&lklfsd, sizeof(lklfsd));

			DbgPrint("Loading LklVfs");

			// fs driver object
			lklfsd.driver = driver;
			lklfsd.physical_device = NULL;
			status = ExInitializeResourceLite(&(lklfsd.global_resource));
			CHECK_OUT(!NT_SUCCESS(status), status);
			resource_init = TRUE;
			// init mounted vcb list
			InitializeListHead(&(lklfsd.vcb_list));
			// create the FS device
			RtlInitUnicodeString(&device_name, LKL_FS_NAME);
			status = IoCreateDevice(driver, 0, &device_name, FILE_DEVICE_DISK_FILE_SYSTEM,
						0, FALSE, &(lklfsd.device));
			CHECK_OUT(!NT_SUCCESS(status), status);

			// init function pointers to the dispatch routines
			InitializeFunctionPointers(driver);
			// init function pointers for the fast I/O
			InitializeFastIO(driver);
			// init callbacks - lklfsd.cache_mgr_callbacks

			//init asynch initialization

			//init event used for async irp processing in kernel thread

			// init look aside lists for our structures
			ccb_cachep = ExAllocatePoolWithTag(NonPagedPool, sizeof(NPAGED_LOOKASIDE_LIST), 'bccL');
			ASSERT(ccb_cachep);
			fcb_cachep = ExAllocatePoolWithTag(NonPagedPool, sizeof(NPAGED_LOOKASIDE_LIST), 'bcfL');
			ASSERT(fcb_cachep);
			irp_context_cachep = ExAllocatePoolWithTag(NonPagedPool, sizeof(NPAGED_LOOKASIDE_LIST), 'priL');
			ASSERT(irp_context_cachep);
			ExInitializeNPagedLookasideList(ccb_cachep, NULL, NULL, 0, sizeof(LKLCCB),'bcC',0);
			ExInitializeNPagedLookasideList(fcb_cachep, NULL, NULL, 0, sizeof(LKLFCB),'bcF',0);
			ExInitializeNPagedLookasideList(irp_context_cachep, NULL, NULL, 0, sizeof(IRPCONTEXT),'cprI',0);

			// create visible link to the fs device for unloading
			RtlInitUnicodeString(&dos_name, LKL_DOS_DEVICE);
			IoCreateSymbolicLink(&dos_name, &device_name);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			status = GetExceptionCode();
		}
try_exit:
		;
	}
	__finally {
		if (!NT_SUCCESS(status))
		{
			// cleanup
			if (lklfsd.device) {
				IoDeleteDevice(lklfsd.device);
				lklfsd.device = NULL;
			}
			if(resource_init)
				ExDeleteResourceLite(&(lklfsd.global_resource));
			if(ccb_cachep)
				ExFreePool(ccb_cachep);
			if(fcb_cachep)
				ExFreePool(fcb_cachep);
			if(irp_context_cachep)
				ExFreePool(irp_context_cachep);

		}
	}
			// register the fs
	if(NT_SUCCESS(status)) {
		IoRegisterFileSystem(lklfsd.device);
		DbgPrint("LklVFS loaded succesfully");
	}
	return status;
}

void InitializeFunctionPointers(PDRIVER_OBJECT driver)
{
	driver->DriverUnload = LklDriverUnload;
	// TODO -functions that MUST be supported
	driver->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = LklFileSystemControl;
	driver->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = LklQueryVolumeInformation;
	driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = LklDeviceControl;
	driver->MajorFunction[IRP_MJ_CREATE] = LklVfsCreate;
	driver->MajorFunction[IRP_MJ_CLOSE]	= LklVfsClose;
	driver->MajorFunction[IRP_MJ_CLEANUP] = LklVfsCleanup;
	driver->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] =LklDummyIrp;
	driver->MajorFunction[IRP_MJ_READ] = LklDummyIrp;
	driver->MajorFunction[IRP_MJ_WRITE] =LklDummyIrp;
	driver->MajorFunction[IRP_MJ_QUERY_INFORMATION] = LklDummyIrp;
	driver->MajorFunction[IRP_MJ_SET_INFORMATION] = LklDummyIrp;
	// these functions are optional
	driver->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = LklDummyIrp;
	driver->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION] = LklDummyIrp;
	driver->MajorFunction[IRP_MJ_SHUTDOWN] = LklDummyIrp;
	driver->MajorFunction[IRP_MJ_LOCK_CONTROL] = LklDummyIrp;
	driver->MajorFunction[IRP_MJ_QUERY_SECURITY] = LklDummyIrp;
	driver->MajorFunction[IRP_MJ_SET_SECURITY] = LklDummyIrp;
	driver->MajorFunction[IRP_MJ_QUERY_EA] = LklDummyIrp;
	driver->MajorFunction[IRP_MJ_SET_EA] = LklDummyIrp;
}

void InitializeFastIO(PDRIVER_OBJECT driver)
{
	static FAST_IO_DISPATCH fiod;

	fiod.SizeOfFastIoDispatch=sizeof(FAST_IO_DISPATCH);
	fiod.FastIoCheckIfPossible=LklFastIoCheckIfPossible;
	fiod.FastIoRead=FsRtlCopyRead;
	fiod.FastIoWrite=FsRtlCopyWrite;

	fiod.FastIoQueryBasicInfo=LklFastIoQueryBasicInfo;
	fiod.FastIoQueryStandardInfo=LklFastIoQueryStandardInfo;
	fiod.FastIoLock=LklFastIoLock;
	fiod.FastIoUnlockSingle=LklFastIoUnlockSingle;
	fiod.FastIoUnlockAll=LklFastIoUnlockAll;
	fiod.FastIoUnlockAllByKey=LklFastIoUnlockAllByKey;
	fiod.FastIoQueryNetworkOpenInfo=LklFastIoQueryNetworkOpenInfo;

	driver->FastIoDispatch = &fiod;

}

void LklDriverUnload(PDRIVER_OBJECT driver)
{
	UNICODE_STRING dos_name;

	DbgPrint("Doing UNLOAD");

	RtlInitUnicodeString(&dos_name, LKL_DOS_DEVICE);
	IoDeleteSymbolicLink(&dos_name);
	ExDeleteNPagedLookasideList(ccb_cachep);
	ExDeleteNPagedLookasideList(fcb_cachep);
	ExDeleteNPagedLookasideList(irp_context_cachep);
	ExFreePool(ccb_cachep);
	ExFreePool(fcb_cachep);
	ExFreePool(irp_context_cachep);
	ExDeleteResourceLite(&lklfsd.global_resource);

	DbgPrint("LklVFS unloaded succesfully");
}