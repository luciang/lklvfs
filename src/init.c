/**
* driver initialization functions
**/

#include <lklvfs.h>
#include<fastio.h>


LKLFSD lklfsd;

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg_path)
{
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING device_name;
	UNICODE_STRING dos_name;

	RtlZeroMemory(&lklfsd, sizeof(lklfsd));

	DbgPrint("Loading LklVfs");

	// fs driver object
	lklfsd.driver = driver;
	lklfsd.physical_device = NULL;
	status = ExInitializeResourceLite(&(lklfsd.global_resource));
	ASSERT(NT_SUCCESS(status));
	// init mounted vcb list
	InitializeListHead(&(lklfsd.vcb_list));
	// create the FS device
	RtlInitUnicodeString(&device_name, LKL_FS_NAME);
	status = IoCreateDevice(driver, 0, &device_name, FILE_DEVICE_DISK_FILE_SYSTEM,
				0, FALSE, &(lklfsd.device));
	if (!NT_SUCCESS(status)) {
			if (lklfsd.device) {
				IoDeleteDevice(lklfsd.device);
				lklfsd.device = NULL;
				return status;
			}
		}

	// init function pointers to the dispatch routines
	InitializeFunctionPointers(driver);
	// init function pointers for the fast I/O
	InitializeFastIO(driver);
	// init callbacks - lklfsd.cache_mgr_callbacks

	//init asynch initialization

	//init event used for async irp processing in kernel thread

	// create visible link to the fs device for unloading
	RtlInitUnicodeString(&dos_name, LKL_DOS_DEVICE);
	IoCreateSymbolicLink(&dos_name, &device_name);

	// register the fs
	IoRegisterFileSystem(lklfsd.device);
	DbgPrint("LklVFS loaded succesfully");
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

	ExDeleteResourceLite(&lklfsd.global_resource);

	DbgPrint("LklVFS unloaded succesfully");
}