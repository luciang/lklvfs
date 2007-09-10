#ifndef _LKL_FASTIO_H
#define _LKL_FASTIO_H

#include <ntifs.h>

BOOLEAN LklFastIoCheckIfPossible (IN PFILE_OBJECT FileObject,IN PLARGE_INTEGER FileOffset,IN ULONG Length,
              IN BOOLEAN Wait,IN ULONG LockKey,IN BOOLEAN CheckForReadOperation,
			  OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject);

BOOLEAN LklFastIoQueryBasicInfo (
	      IN PFILE_OBJECT             FileObject,
              IN BOOLEAN                  Wait,
              OUT PFILE_BASIC_INFORMATION Buffer,
              OUT PIO_STATUS_BLOCK        IoStatus,
              IN PDEVICE_OBJECT           DeviceObject);

BOOLEAN LklFastIoQueryStandardInfo (
                IN PFILE_OBJECT                 FileObject,
                IN BOOLEAN                      Wait,
                OUT PFILE_STANDARD_INFORMATION  Buffer,
                OUT PIO_STATUS_BLOCK            IoStatus,
                IN PDEVICE_OBJECT               DeviceObject);

BOOLEAN LklFastIoQueryNetworkOpenInfo (
     IN PFILE_OBJECT                     FileObject,
     IN BOOLEAN                          Wait,
     OUT PFILE_NETWORK_OPEN_INFORMATION  Buffer,
     OUT PIO_STATUS_BLOCK                IoStatus,
     IN PDEVICE_OBJECT                   DeviceObject);

BOOLEAN
LklFastIoLock (
           IN PFILE_OBJECT         FileObject,
           IN PLARGE_INTEGER       FileOffset,
           IN PLARGE_INTEGER       Length,
           IN PEPROCESS            Process,
           IN ULONG                Key,
           IN BOOLEAN              FailImmediately,
           IN BOOLEAN              ExclusiveLock,
           OUT PIO_STATUS_BLOCK    IoStatus,
           IN PDEVICE_OBJECT       DeviceObject);

BOOLEAN
LklFastIoUnlockSingle (
               IN PFILE_OBJECT         FileObject,
               IN PLARGE_INTEGER       FileOffset,
               IN PLARGE_INTEGER       Length,
               IN PEPROCESS            Process,
               IN ULONG                Key,
               OUT PIO_STATUS_BLOCK    IoStatus,
               IN PDEVICE_OBJECT       DeviceObject);

BOOLEAN
LklFastIoUnlockAll (
            IN PFILE_OBJECT         FileObject,
            IN PEPROCESS            Process,
            OUT PIO_STATUS_BLOCK    IoStatus,
            IN PDEVICE_OBJECT       DeviceObject);

BOOLEAN
LklFastIoUnlockAllByKey (
             IN PFILE_OBJECT         FileObject,
             IN PEPROCESS            Process,
             IN ULONG                Key,
             OUT PIO_STATUS_BLOCK    IoStatus,
             IN PDEVICE_OBJECT       DeviceObject);

#endif

