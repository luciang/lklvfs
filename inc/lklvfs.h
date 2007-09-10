#ifndef _LKL_VFS_H
#define _LKL_VFS_H

#include <ntifs.h>

#define LKL_DECLARE_NONSTD(type)     type __cdecl

#define LKL_DEVICE		L"\\DosDevices\\F:"
#define LKL_FS_NAME		L"\\lkl"
#define LKL_DOS_DEVICE	L"\\DosDevices\\lkl"

#define CHECK_OUT(cond, S)		{if(cond){status=S;goto try_exit;}}
#define FLAG_ON(flag, val)		((BOOLEAN)((((flag)&(val))!=0)))
#define SET_FLAG(flag, val)		((flag)|=(val))
#define CLEAR_FLAG(flag, val)	((flag)&=~(val))
#define RELEASE(res) (ExReleaseResourceForThreadLite((res), ExGetCurrentResourceThread()))
#define	QUAD_ALIGN(val)			((((ULONG)(val))+7)&0xfffffff8)

typedef struct lkl_fsd {
	ERESOURCE				global_resource;
	PDRIVER_OBJECT			driver;
	PDEVICE_OBJECT			device;
	// use it for now:
	PDEVICE_OBJECT			physical_device;
	LIST_ENTRY				vcb_list;

	FAST_IO_DISPATCH		fast_io_dispatch;
	CACHE_MANAGER_CALLBACKS	cache_mgr_callbacks;

	HANDLE					linux_thread;
	PVOID					mem_zone;
} LKLFSD;

extern LKLFSD lklfsd;

typedef struct lkl_vcb
{
	FSRTL_COMMON_FCB_HEADER	common_header;

	SECTION_OBJECT_POINTERS	section_object;
	ERESOURCE				vcb_resource;
	LIST_ENTRY				next;					// next mounted vcb
	PVPB					vpb;					// vpb
	ULONG					open_count;				// how many open handles for files in this volume
	ULONG					reference_count;		// how many files referenced in this volume
	LIST_ENTRY				fcb_list;				// head of open files list
	LIST_ENTRY				next_notify_irp;		// used for direrctory notification
	KMUTEX					notify_irp_mutex;		//
	PDEVICE_OBJECT			vcb_device;				// the volume device object
	PDEVICE_OBJECT			target_device;			// the physical device object
	UCHAR					*volume_path;			// volume path
	//more fields here

} LKLVCB, * PLKLVCB;



/* init.c */

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg_path);
void InitializeFunctionPointers(PDRIVER_OBJECT driver);
void InitializeFastIO(PDRIVER_OBJECT driver);
VOID LklDriverUnload(PDRIVER_OBJECT driver);
NTSTATUS run_linux_kernel();
void unload_linux_kernel();

/* fscontrol.c */
NTSTATUS LklFileSystemControl(PDEVICE_OBJECT device, PIRP irp);
NTSTATUS LklMountVolume(PIRP irp, PIO_STACK_LOCATION stack_location);
NTSTATUS LklUserFileSystemRequest(PIRP irp, PIO_STACK_LOCATION stack_location);
NTSTATUS LklLockVolume(PIRP irp, PIO_STACK_LOCATION stack_location);
void LklPurgeVolume(PLKLVCB vcb, BOOLEAN flush_before_purge);

void LklSetVpbFlag(PVPB vpb, USHORT flag);
void LklClearVpbFlag(PVPB vpb, USHORT flag);
NTSTATUS LklUnlockVolume(PIRP irp, PIO_STACK_LOCATION stack_location);
NTSTATUS LklDismountVolume(PIRP irp, PIO_STACK_LOCATION stack_location);
NTSTATUS LklIsVolumeMounted(PIRP irp, PIO_STACK_LOCATION stack_location);
NTSTATUS LklVerifyVolume(PIRP irp, PIO_STACK_LOCATION stack_location);

NTSTATUS LklUmount(PDEVICE_OBJECT dev, PFILE_OBJECT file);
NTSTATUS LklMount(IN PDEVICE_OBJECT dev,IN PVPB vpb);

/* alloc.c */
void LklCreateVcb(PDEVICE_OBJECT volume_dev, PDEVICE_OBJECT target_dev, PVPB vpb,
					  PLARGE_INTEGER alloc_size);
void LklFreeVcb(PLKLVCB vcb);

/* close.c */
NTSTATUS LklClose(PDEVICE_OBJECT device, PIRP irp);

/* create.c */
NTSTATUS LklCreate(PDEVICE_OBJECT device, PIRP irp);

/* misc.c */
void LklCompleteRequest(PIRP irp, NTSTATUS status);
NTSTATUS LklDummyIrp(PDEVICE_OBJECT dev_obj, PIRP irp);


#endif