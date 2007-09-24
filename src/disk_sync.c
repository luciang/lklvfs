#include <lklvfs.h>
#include <file_disk.h>
#include <asm/callbacks.h>

#define KeBugOn(x) if (x) { DbgPrint("bug %s:%d\n", __FUNCTION__, __LINE__); while (1); }

NTSTATUS ReadBlockDevice(IN PDEVICE_OBJECT DeviceObject, IN PLARGE_INTEGER Offset,
							  IN ULONG Length, IN OUT PVOID Buffer)
{
	KEVENT			Event;
	PIRP			Irp;
	IO_STATUS_BLOCK	IoStatus;
	NTSTATUS		Status = STATUS_UNSUCCESSFUL;

	ASSERT(DeviceObject != NULL);
	ASSERT(Offset != NULL);
	ASSERT(Buffer != NULL);

		KeInitializeEvent(&Event, NotificationEvent, FALSE);

		Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ, DeviceObject, Buffer, Length, Offset,
			&Event, &IoStatus);
		if (!Irp) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		Status = IoCallDriver(DeviceObject, Irp);
		if (Status == STATUS_PENDING)
		{
			KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
			Status = IoStatus.Status;
		}
    
	return Status;
}

NTSTATUS WriteBlockDevice(IN PDEVICE_OBJECT DeviceObject, IN PLARGE_INTEGER Offset,
							   IN ULONG Length, IN PVOID Buffer)
{
    KEVENT          Event;
    PIRP            Irp;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS        Status;

    ASSERT(DeviceObject != NULL);
    ASSERT(Offset != NULL);
    ASSERT(Buffer != NULL);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_WRITE, DeviceObject, Buffer, Length, Offset,
        &Event, &IoStatus);

    if (!Irp) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    
    return Status;
}

void * _file_open()
	{
		DbgPrint("open disk!");	
		return ((PLKLVCB) lklfsd.mounted_volume->DeviceExtension)->target_device;
	}

unsigned long _file_sectors()
{
     PLKLVCB dev = (PLKLVCB)lklfsd.mounted_volume->DeviceExtension;
     unsigned long size;
	DbgPrint("get number of disk sectors");
     // return the number of sectors that this device has
     size = dev->disk_geometry.Cylinders.QuadPart *
            dev->disk_geometry.TracksPerCylinder *
			dev->disk_geometry.SectorsPerTrack;
	
	return size;
         
}

void _file_rw(void * f, unsigned long start_sector, unsigned long nsect, char * buffer, int dir)
	{
		PDEVICE_OBJECT dev = (PDEVICE_OBJECT) f;
		NTSTATUS status;
        ULONG blk_size;
    	ULONG bytes_cnt;
    	LARGE_INTEGER offset;
 	
    	blk_size = dev->SectorSize;
    	bytes_cnt = blk_size * nsect;
    	offset.QuadPart = start_sector * blk_size;
    	
	    if(dir)
	    	status = WriteBlockDevice(dev, &offset, bytes_cnt,(PVOID) buffer);
    	else
    	    status = ReadBlockDevice(dev, &offset, bytes_cnt,(PVOID) buffer);
        
		KeBugOn (status != STATUS_SUCCESS);
	}

