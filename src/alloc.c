/**
* allocation related stuff
**/

#include<lklvfs.h>

void LklCreateVcb(PDEVICE_OBJECT volume_dev, PDEVICE_OBJECT target_dev, PVPB vpb,
					  PLARGE_INTEGER alloc_size)
{
	NTSTATUS status = STATUS_SUCCESS;
	PLKLVCB vcb = NULL;

	vcb = (PLKLVCB)(volume_dev->DeviceExtension);
	RtlZeroMemory(vcb, sizeof(LKLVCB));

	status = ExInitializeResourceLite(&(vcb->vcb_resource));
	ASSERT(NT_SUCCESS(status));

	vcb->target_device = target_dev;
	vcb->vcb_device = volume_dev;
	vcb->vpb = vpb;

	InitializeListHead(&(vcb->fcb_list));
	InitializeListHead(&(vcb->next_notify_irp));

	KeInitializeMutex(&(vcb->notify_irp_mutex), 0);

	vcb->common_header.AllocationSize.QuadPart = alloc_size->QuadPart;
	vcb->common_header.FileSize.QuadPart = alloc_size->QuadPart;
	vcb->common_header.ValidDataLength.LowPart = 0xFFFFFFFF;
	vcb->common_header.ValidDataLength.HighPart = 0x7FFFFFFF;
	vcb->common_header.IsFastIoPossible = FastIoIsNotPossible;
	vcb->common_header.Resource = &(vcb->vcb_resource);

	ExAcquireResourceExclusiveLite(&(lklfsd.global_resource), TRUE);
	InsertTailList(&(lklfsd.vcb_list), &(vcb->next));
	RELEASE(&(lklfsd.global_resource));
}

void LklFreeVcb(PLKLVCB vcb)
{
	LklClearVpbFlag(vcb->vpb, VPB_MOUNTED);

	ExAcquireResourceExclusiveLite(&lklfsd.global_resource, TRUE);
	RemoveEntryList(&vcb->next);
	RELEASE(&lklfsd.global_resource);

	ExDeleteResourceLite(&vcb->vcb_resource);

	IoDeleteDevice(vcb->vcb_device);
}


