/**
* Fast I/O support
**/

#include <fastio.h>

BOOLEAN DDKAPI VfsFastIoCheckIfPossible (IN PFILE_OBJECT FileObject,
					 IN PLARGE_INTEGER FileOffset,
					 IN ULONG Length, IN BOOLEAN Wait,
					 IN ULONG LockKey,
					 IN BOOLEAN CheckForReadOperation,
					 OUT PIO_STATUS_BLOCK IoStatus,
					 IN PDEVICE_OBJECT DeviceObject)
{
	DbgPrint("Check Fast IO");
	return TRUE;
}

BOOLEAN DDKAPI VfsFastIoQueryBasicInfo (IN PFILE_OBJECT FileObject,
					IN BOOLEAN Wait,
					OUT PFILE_BASIC_INFORMATION Buffer,
					OUT PIO_STATUS_BLOCK IoStatus,
					IN PDEVICE_OBJECT DeviceObject)
{
	DbgPrint("Fast Io Query Basic Info");
	return FALSE;
}

BOOLEAN DDKAPI VfsFastIoQueryStandardInfo (IN PFILE_OBJECT FileObject,
					   IN BOOLEAN Wait,
					   OUT PFILE_STANDARD_INFORMATION Buffer,
					   OUT PIO_STATUS_BLOCK IoStatus,
					   IN PDEVICE_OBJECT DeviceObject)
{
	DbgPrint("Fast Io Query standard Info");
	return FALSE;
}

BOOLEAN DDKAPI VfsFastIoQueryNetworkOpenInfo (IN PFILE_OBJECT FileObject,
					      IN BOOLEAN Wait,
					      OUT PFILE_NETWORK_OPEN_INFORMATION Buffer,
					      OUT PIO_STATUS_BLOCK IoStatus,
					      IN PDEVICE_OBJECT DeviceObject)
{
	DbgPrint( "Fast Io query Network Info");
	return FALSE;
}

BOOLEAN DDKAPI VfsFastIoLock (IN PFILE_OBJECT FileObject,
			      IN PLARGE_INTEGER FileOffset,
			      IN PLARGE_INTEGER Length, IN PEPROCESS Process,
			      IN ULONG Key, IN BOOLEAN FailImmediately,
			      IN BOOLEAN ExclusiveLock,
			      OUT PIO_STATUS_BLOCK IoStatus,
			      IN PDEVICE_OBJECT DeviceObject)
{
	DbgPrint( "Fast Io Lock");
	return FALSE;
}




BOOLEAN DDKAPI VfsFastIoUnlockSingle (IN PFILE_OBJECT FileObject,
				      IN PLARGE_INTEGER FileOffset,
				      IN PLARGE_INTEGER Length,
				      IN PEPROCESS Process, IN ULONG Key,
				      OUT PIO_STATUS_BLOCK IoStatus,
				      IN PDEVICE_OBJECT DeviceObject)
{
	DbgPrint( "Fast Io unlock single");
	return FALSE;
}


BOOLEAN DDKAPI VfsFastIoUnlockAll (IN PFILE_OBJECT FileObject,
				   IN PEPROCESS Process,
				   OUT PIO_STATUS_BLOCK IoStatus,
				   IN PDEVICE_OBJECT DeviceObject)
{
	DbgPrint( "Fast Io Unlock All");
	return FALSE;
}


BOOLEAN DDKAPI VfsFastIoUnlockAllByKey (IN PFILE_OBJECT FileObject,
					PVOID Process, ULONG Key,
					OUT PIO_STATUS_BLOCK IoStatus,
					IN PDEVICE_OBJECT DeviceObject)
{
	DbgPrint( "Fast Io unlock all by key");
	return FALSE;
}
