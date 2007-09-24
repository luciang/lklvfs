#include <lklvfs.h>
#include <file_disk-async.h>
#include <asm/callbacks.h>

#define KeBugOn(x) if (x) { DbgPrint("bug %s:%d\n", __FUNCTION__, __LINE__); while (1); }

NTSTATUS PitixReadBlockDevice(IN PDEVICE_OBJECT DeviceObject, IN PLARGE_INTEGER Offset,
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
    DbgPrint("Done Read!");
	return Status;
}

NTSTATUS PitixWriteBlockDevice(IN PDEVICE_OBJECT DeviceObject, IN PLARGE_INTEGER Offset,
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
    DbgPrint("Done write!");
    return Status;
}

NTSTATUS ReadWriteCompletion(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PVOID Context)
{
	PMDL Mdl;
	struct completion_status *cs = (struct completion_status *) Context;

    ASSERT(Irp != NULL);

    *Irp->UserIosb = Irp->IoStatus;
	if(NT_SUCCESS(Irp->IoStatus.Status))
		cs->status=1;
	else
		cs->status=0;
	
    if (Irp->AssociatedIrp.SystemBuffer && FlagOn(Irp->Flags, IRP_DEALLOCATE_BUFFER)) {
        ExFreePool(Irp->AssociatedIrp.SystemBuffer);
    }
    else {
        while ((Mdl = Irp->MdlAddress)) {
            Irp->MdlAddress = Mdl->Next;
            IoFreeMdl(Mdl);
        }
    }

    IoFreeIrp(Irp);
	
	linux_trigger_irq_with_data(FILE_DISK_IRQ, cs);
    DbgPrint("FINISHED READ COMPLETION\n");

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS ReadWritePhysicalBlocks(IN PDEVICE_OBJECT block_dev, PVOID buffer,IN ULONG start_block,IN ULONG blk_cnt, int dir, struct completion_status *cs)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	ULONG blk_size;
	ULONG bytes_cnt;
	PIRP irp;
	IO_STATUS_BLOCK iosb;
	LARGE_INTEGER offset;

	DbgPrint("I/O op: dev: %p start_block: %d count: %u\n", block_dev, (int) start_block, blk_cnt);

	blk_size = block_dev->SectorSize;
	bytes_cnt = blk_size * blk_cnt;
	offset.QuadPart = start_block * blk_size;

	if(dir)
		irp = IoBuildAsynchronousFsdRequest(IRP_MJ_WRITE, block_dev, buffer, bytes_cnt, &offset, &iosb);
	else
		irp = IoBuildAsynchronousFsdRequest(IRP_MJ_READ, block_dev, buffer, bytes_cnt, &offset, &iosb);
	CHECK_OUT(!irp, STATUS_INSUFFICIENT_RESOURCES);

	IoSetCompletionRoutine(irp, ReadWriteCompletion, cs, TRUE, TRUE, TRUE);

//	status = IoCallDriver(block_dev, irp);
		
try_exit:
		;
		
	return status;
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

void _file_rw_async(void * f, unsigned long start_sector, unsigned long nsect, char * buffer, 
					int dir, struct completion_status *cs)
	{
		PDEVICE_OBJECT dev = (PDEVICE_OBJECT) f;
		NTSTATUS status;
        ULONG blk_size;
    	ULONG bytes_cnt;
    	LARGE_INTEGER offset;
 	
    	blk_size = dev->SectorSize;
    	bytes_cnt = blk_size * nsect;
    	offset.QuadPart = start_sector * blk_size;
    	DbgPrint("I/O op: dev: %p start_block: %d count: %u\n", dev, (int) start_sector, nsect);
	    if(dir)
	    	status = PitixWriteBlockDevice(dev, &offset, bytes_cnt,(PVOID) buffer);
    	else
    	    status = PitixReadBlockDevice(dev, &offset, bytes_cnt,(PVOID) buffer);
         if(NT_SUCCESS(status))
         cs->status = 1;
         else
         cs->status =0;
         linux_trigger_irq_with_data(FILE_DISK_IRQ, cs);
		KeBugOn (status != STATUS_SUCCESS);
	}
