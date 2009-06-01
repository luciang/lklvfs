/**
* driver initialization 
* TODOs: -
**/

#include <lklvfs.h>
#include <fastio.h>
#include <asm/env.h>

LKLFSD lklfsd;
PNPAGED_LOOKASIDE_LIST ccb_cachep;
PNPAGED_LOOKASIDE_LIST fcb_cachep;
PNPAGED_LOOKASIDE_LIST irp_context_cachep;


NTSTATUS DDKAPI DriverEntry(IN PDRIVER_OBJECT driver,IN PUNICODE_STRING reg_path)
{
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING device_name;
	UNICODE_STRING dos_name;
	BOOLEAN resource_init = FALSE;

	ccb_cachep = NULL;
	fcb_cachep = NULL;
	irp_context_cachep = NULL;

	RtlZeroMemory(&lklfsd, sizeof(lklfsd));

	DbgPrint("Loading LklVfs - compiled at: " __TIME__ " on " __DATE__);

	// fs driver object
	lklfsd.driver = driver;
	status = ExInitializeResourceLite(&(lklfsd.global_resource));
	CHECK_OUT(!NT_SUCCESS(status), status);
	resource_init = TRUE;
		
	status = InitializeSysWrappers();
	CHECK_OUT(!NT_SUCCESS(status), status);
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
	// init cache manager callbacks
	lklfsd.cache_mgr_callbacks.AcquireForLazyWrite = VfsAcqLazyWrite;
	lklfsd.cache_mgr_callbacks.ReleaseFromLazyWrite = VfsRelLazyWrite;
	lklfsd.cache_mgr_callbacks.AcquireForReadAhead = VfsAcqReadAhead;
	lklfsd.cache_mgr_callbacks.ReleaseFromReadAhead = VfsRelReadAhead;			


	//init asynch initialization -- no need for it because we use system worker threads
	//init event used for async irp processing in our kernel thread(s) -- the same

	// init lookaside lists for our structures
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

	lkl_env_init(64 * 1024 * 1024);

try_exit:
	if (!NT_SUCCESS(status)) {
		DbgPrint("LKLVFS load failure");
		// cleanup
		FreeSysWrapperResources();
		
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

	// register the fs
	if(NT_SUCCESS(status)) {
		IoRegisterFileSystem(lklfsd.device);
		DbgPrint("LklVFS loaded succesfully");
	}
	return status;
}

VOID InitializeFunctionPointers(PDRIVER_OBJECT driver)
{
	// functions that MUST be supported
	driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = VfsDeviceControl;
	driver->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = VfsFileSystemControl;
	driver->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = VfsQueryVolumeInformation;
	driver->MajorFunction[IRP_MJ_QUERY_INFORMATION] = VfsQueryInformation;
	driver->MajorFunction[IRP_MJ_SET_INFORMATION] = VfsSetInformation;
	driver->MajorFunction[IRP_MJ_CREATE] = VfsCreate;
	driver->MajorFunction[IRP_MJ_CLOSE]	= VfsClose;
	driver->MajorFunction[IRP_MJ_CLEANUP] = VfsCleanup;
	driver->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] = VfsDirectoryControl;
	driver->MajorFunction[IRP_MJ_READ] = VfsRead;
	driver->MajorFunction[IRP_MJ_WRITE] =VfsWrite;

	// these functions are optional
	driver->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = VfsDummyIrp;
	driver->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION] = VfsDummyIrp;
	driver->MajorFunction[IRP_MJ_SHUTDOWN] = VfsDummyIrp;
	driver->MajorFunction[IRP_MJ_LOCK_CONTROL] = VfsDummyIrp;
	driver->MajorFunction[IRP_MJ_QUERY_SECURITY] = VfsDummyIrp;
	driver->MajorFunction[IRP_MJ_SET_SECURITY] = VfsDummyIrp;
	driver->MajorFunction[IRP_MJ_QUERY_EA] = VfsDummyIrp;
	driver->MajorFunction[IRP_MJ_SET_EA] = VfsDummyIrp;
}

VOID InitializeFastIO(PDRIVER_OBJECT driver)
{
	static FAST_IO_DISPATCH fiod;

	fiod.SizeOfFastIoDispatch=sizeof(FAST_IO_DISPATCH);
	fiod.FastIoCheckIfPossible=VfsFastIoCheckIfPossible;
	fiod.FastIoRead=FsRtlCopyRead;
	fiod.FastIoWrite=FsRtlCopyWrite;

	fiod.FastIoQueryBasicInfo=VfsFastIoQueryBasicInfo;
	fiod.FastIoQueryStandardInfo=VfsFastIoQueryStandardInfo;
	fiod.FastIoLock=VfsFastIoLock;
	fiod.FastIoUnlockSingle=VfsFastIoUnlockSingle;
	fiod.FastIoUnlockAll=VfsFastIoUnlockAll;
	fiod.FastIoUnlockAllByKey=VfsFastIoUnlockAllByKey;
	fiod.FastIoQueryNetworkOpenInfo=VfsFastIoQueryNetworkOpenInfo;

	driver->FastIoDispatch = &fiod;

}

VOID DDKAPI DriverUnload(PDRIVER_OBJECT driver)
{
	UNICODE_STRING dos_name;
	
	FreeSysWrapperResources();
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
