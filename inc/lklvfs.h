#ifndef _LKL_VFS_H
#define _LKL_VFS_H

#include <ddk/ntifs.h>

#undef FASTCALL


#define LKL_DEVICE		L"\\DosDevices\\F:"
#define LKL_FS_NAME		L"\\lkl"
#define LKL_DOS_DEVICE	L"\\DosDevices\\lkl"

#define CHECK_OUT(cond, S)		{if(cond){status=S;goto try_exit;}}
#define FLAG_ON(flag, val)		((BOOLEAN)((((flag)&(val))!=0)))
#define SET_FLAG(flag, val)		((flag)|=(val))
#define CLEAR_FLAG(flag, val)	((flag)&=~(val))
#define RELEASE(res) (ExReleaseResourceForThreadLite((res), ExGetCurrentResourceThread()))
#define TRY_RETURN(S)			{status=S;goto try_exit;}

#define VFS_UNLOAD_PENDING					0x00000001
#define	IOCTL_PREPARE_TO_UNLOAD \
		CTL_CODE(FILE_DEVICE_UNKNOWN, 2048, METHOD_NEITHER, FILE_WRITE_ACCESS)

// used for identifier
#define	CCB					(0xfdecba02)
#define	FCB					(0xfdecba03)
#define	VCB					(0xfdecba04)
#define	IRP_CONTEXT			(0xfdecba05)

// identifier used for each defined data structure
typedef struct lklvfs_identifier {
	ULONG	type;
	ULONG	size;
} LKLVFSID;

// irp context flags
#define	VFS_IRP_CONTEXT_CAN_BLOCK			0x00000001
#define	VFS_IRP_CONTEXT_WRITE_THROUGH		0x00000002
#define	VFS_IRP_CONTEXT_EXCEPTION			0x00000004
#define	VFS_IRP_CONTEXT_DEFERRED_WRITE		0x00000008
#define	VFS_IRP_CONTEXT_ASYNC_PROCESSING	0x00000010
#define	VFS_IRP_CONTEXT_NOT_TOP_LEVEL		0x00000020
#define	VFS_IRP_CONTEXT_NOT_FROM_ZONE		0x80000000

// irp context - used to save useful context information for later processing of irps
typedef struct irp_context {
	LKLVFSID				id;
	ULONG					flags;
	UCHAR					major_function;
	UCHAR					minor_function;
	PIRP					irp;
	PDEVICE_OBJECT			target_device;
	NTSTATUS				saved_exception_code;
	PIO_WORKITEM			work_item;

} IRPCONTEXT, * PIRPCONTEXT;

typedef struct lkl_fsd {
	ERESOURCE				global_resource;
	PDRIVER_OBJECT			driver;
	PDEVICE_OBJECT			device;		// fs device
	ULONG					flags;		// flags- one is used for unload
	LIST_ENTRY				vcb_list;	// head of mounted volume list

	FAST_IO_DISPATCH		fast_io_dispatch;
	CACHE_MANAGER_CALLBACKS	cache_mgr_callbacks; // cache manager callbacks
// temporary use:
	PDEVICE_OBJECT			physical_device; // current mounted device
	HANDLE					linux_thread;

} LKLFSD;

// global data used by our driver
extern LKLFSD lklfsd;
// lookaside lists for fcb, ccb and irp_context structures
extern PNPAGED_LOOKASIDE_LIST ccb_cachep;
extern PNPAGED_LOOKASIDE_LIST fcb_cachep;
extern PNPAGED_LOOKASIDE_LIST irp_context_cachep;

// vcb flags
#define VFS_VCB_FLAGS_VOLUME_MOUNTED	0x00000001
#define	VFS_VCB_FLAGS_VOLUME_LOCKED		0x00000002
#define	VFS_VCB_FLAGS_BEING_DISMOUNTED	0x00000004
#define	VFS_VCB_FLAGS_SHUTDOWN			0x00000008
#define	VFS_VCB_FLAGS_VOLUME_READ_ONLY	0x00000010
#define	VFS_VCB_FLAGS_VCB_INITIALIZED	0x00000020

typedef struct lkl_vcb
{
	// required header - this vcb is used as fcb for volume open/close, etc.
	FSRTL_COMMON_FCB_HEADER		common_header;
	SECTION_OBJECT_POINTERS		section_object;
	ERESOURCE					vcb_resource;
	ERESOURCE					paging_resource;

	LKLVFSID					id;						// this tells that i'm a vcb
	LIST_ENTRY					next;					// next mounted vcb
	PVPB						vpb;					// vpb
	ULONG						open_count;				// how many open handles for files in this volume
	ULONG						reference_count;		// how many files referenced in this volume
	LIST_ENTRY					fcb_list;				// head of open files list
	LIST_ENTRY					next_notify_irp;		// used for direrctory notification
	KMUTEX						notify_irp_mutex;
	ULONG						flags;
	PDEVICE_OBJECT				vcb_device;				// the volume device object
	PDEVICE_OBJECT				target_device;			// the physical device object
	UCHAR						*volume_path;			// volume path
	//more fields here

} LKLVCB, * PLKLVCB;

// fcb flags
#define	VFS_FCB_IN_INIT						0x00000001
#define	VFS_FCB_IN_TEARDOWN					0x00000002
#define	VFS_FCB_PAGE_FILE					0x00000004
#define	VFS_FCB_DIRECTORY					0x00000008
#define	VFS_FCB_WRITE_THROUGH				0x00000020
#define	VFS_FCB_MAPPED						0x00000040
#define	VFS_FCB_FAST_IO_READ_IN_PROGESS		0x00000080
#define	VFS_FCB_FAST_IO_WRITE_IN_PROGESS	0x00000100
#define	VFS_FCB_DELETE_ON_CLOSE				0x00000200
#define	VFS_FCB_MODIFIED					0x00000400
#define	VFS_FCB_ACCESSED					0x00000800
#define	VFS_FCB_READ_ONLY					0x00001000
#define	VFS_FCB_BLOCKS_INITIALIZED			0x00008000
#define	VFS_FCB_SPECIAL_FILE				0x00010000
#define	VFS_FCB_HIDDEN_FILE					0x00020000
#define	VFS_FCB_NOT_FROM_ZONE				0x80000000

typedef struct lkl_fcb
{
	// required header
	FSRTL_COMMON_FCB_HEADER		common_header;
	SECTION_OBJECT_POINTERS		section_object;
	ERESOURCE					fcb_resource;
	ERESOURCE					paging_resource;

	LKLVFSID					id;					// this tells that i'm a fcb
	PLKLVCB						vcb;				// vcb we belong to
	LIST_ENTRY					next;				// next open fcb in vcb
	ULONG						flags;				// flag
	LIST_ENTRY					ccb_list;			// head of ccb list
	SHARE_ACCESS				share_access;		// share access
	ULONG						reference_count;	// references to this fcb
	ULONG						handle_count;		// open handles to this file
	LARGE_INTEGER				creation_time;		// some times kept here for easier access
	LARGE_INTEGER				lastaccess_time;
	LARGE_INTEGER				lastwrite_time;
	// more fields here
	UNICODE_STRING				name;
	ULONG						ino;

} LKLFCB, * PLKLFCB;

typedef struct lkl_ccb
{
	LKLVFSID				id;				// i'm a ccb
	PLKLFCB					fcb;			// fcb we belong to
	LIST_ENTRY				next;			// next ccb in list of ccbs belonging to this fcb
	PFILE_OBJECT			file_obj;		// file obj representing this open file
	ULONG					flags;			// flags
	LARGE_INTEGER			offset;			// current pointer offest
	UNICODE_STRING			search_pattern;	// search pattern if this file is a dir
	ULONG					user_time;		// maintain time for user specified time
	//more fields here
	ULONG					fd;				//file descriptor

} LKLCCB, * PLKLCCB;


/* init.c */

void InitializeFunctionPointers(PDRIVER_OBJECT driver);
void InitializeFastIO(PDRIVER_OBJECT driver);
VOID DDKAPI DriverUnload(PDRIVER_OBJECT driver);

/* fscontrol.c */
NTSTATUS DDKAPI VfsFileSystemControl(PDEVICE_OBJECT device, PIRP irp);
NTSTATUS DDKAPI VfsMountVolume(PIRP irp, PIO_STACK_LOCATION stack_location);
NTSTATUS DDKAPI VfsUserFileSystemRequest(PIRP irp, PIO_STACK_LOCATION stack_location);
NTSTATUS DDKAPI VfsLockVolume(PIRP irp, PIO_STACK_LOCATION stack_location);
void     DDKAPI VfsPurgeVolume(PLKLVCB vcb, BOOLEAN flush_before_purge);
NTSTATUS DDKAPI VfsUnLockVolume(PIRP irp, PIO_STACK_LOCATION stack_location);
NTSTATUS DDKAPI VfsUnmountVolume(PIRP irp, PIO_STACK_LOCATION stack_location);
NTSTATUS DDKAPI VfsIsVolumeMounted(PIRP irp, PIO_STACK_LOCATION stack_location);
NTSTATUS DDKAPI VfsVerifyVolume(PIRP irp, PIO_STACK_LOCATION stack_location);

void SetVpbFlag(PVPB vpb, USHORT flag);
void ClearVpbFlag(PVPB vpb, USHORT flag);
NTSTATUS VfsUmount(PDEVICE_OBJECT dev, PFILE_OBJECT file);
NTSTATUS VfsMount(IN PDEVICE_OBJECT dev,IN PVPB vpb);

/* devcontrol.c */
NTSTATUS DDKAPI VfsDeviceControl(PDEVICE_OBJECT device, PIRP irp);

/* alloc.c */
void       CreateVcb(PDEVICE_OBJECT volume_dev, PDEVICE_OBJECT target_dev, PVPB vpb,
					  PLARGE_INTEGER alloc_size);
void       FreeVcb(PLKLVCB vcb);
NTSTATUS   CreateFcb(PLKLFCB *new_fcb, PFILE_OBJECT file_obj, PLKLVCB vcb, ULONG ino);
void       FreeFcb(PLKLFCB fcb);
NTSTATUS   CreateNewCcb(PLKLCCB *new_ccb, PLKLFCB fcb, PFILE_OBJECT file_obj);
void       CloseAndFreeCcb(PLKLCCB ccb);
PIRPCONTEXT AllocIrpContext(PIRP irp, PDEVICE_OBJECT target_device);
void        FreeIrpContext(PIRPCONTEXT irp_context);

/* cleanup.c */
NTSTATUS DDKAPI VfsCleanup(PDEVICE_OBJECT device, PIRP irp);
NTSTATUS        CommonCleanup(PIRPCONTEXT irp_context, PIRP irp);

/* close.c */
NTSTATUS DDKAPI VfsClose(PDEVICE_OBJECT device, PIRP irp);
NTSTATUS        CommonClose(PIRPCONTEXT irp_context, PIRP irp);

/* create.c */
NTSTATUS DDKAPI VfsCreate(PDEVICE_OBJECT device, PIRP irp);
NTSTATUS        CommonCreate(PIRPCONTEXT irp_context, PIRP irp);

/* dircontrol.c */
NTSTATUS DDKAPI VfsDirectoryControl(PDEVICE_OBJECT device, PIRP irp);
NTSTATUS        CommonDirectoryControl(PIRPCONTEXT irp_context, PIRP irp);

/* misc.c */
NTSTATUS DDKAPI VfsDummyIrp(PDEVICE_OBJECT dev_obj, PIRP irp);
void LklCompleteRequest(PIRP irp, NTSTATUS status);
BOOLEAN LklIsIrpTopLevel(PIRP irp);
VOID CharToWchar(PWCHAR Destination, PCHAR Source, ULONG Length);
void VfsReportError(const char * string);

NTSTATUS run_linux_kernel();
void unload_linux_kernel();

/* workqueue.c */
NTSTATUS LklPostRequest(PIRPCONTEXT irp_context, PIRP irp);
/* geninfo.c */
NTSTATUS DDKAPI VfsQueryVolumeInformation(PDEVICE_OBJECT dev_obj, PIRP irp);
NTSTATUS DDKAPI VfsQueryInformation(PDEVICE_OBJECT device ,PIRP irp);
NTSTATUS DDKAPI VfsSetInformation(PDEVICE_OBJECT device ,PIRP irp);

/* cmcallbacks.c */

void    DDKAPI   VfsRelLazyWrite(PVOID context);
BOOLEAN DDKAPI   VfsAcqLazyWrite(PVOID context, BOOLEAN wait);
void    DDKAPI   VfsRelLazyWrite(PVOID context);
BOOLEAN DDKAPI   VfsAcqReadAhead(PVOID context, BOOLEAN wait);
void    DDKAPI   VfsRelReadAhead(PVOID context);

#endif
