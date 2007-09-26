#include <lklvfs.h>
#undef FASTCALL
#include <linux/stat.h>
#include <asm/callbacks.h>
#include <asm/unistd.h>
#undef FASTCALL
#include <drivers/disk.h>

struct _thread_info {
        HANDLE th;
        KSEMAPHORE sched_sem;
	int dead;
};

struct kernel_thread_helper_arg {
        int (*fn)(void*);
        void *arg;
        struct _thread_info *pti;
};

int linux_thread_info_size=sizeof(struct _thread_info);

void* linux_thread_info_alloc(void)
{
        struct _thread_info *pti=ExAllocatePool(PagedPool, sizeof(*pti));

	if (!pti)
		KeBugCheck(0);

        KeInitializeSemaphore(&pti->sched_sem, 0, 100);
	pti->dead=0;

	return pti;
}

static int debug_thread_count;
static KEVENT debug_thread_event;

void linux_context_switch(void *prev, void *next)
{
        struct _thread_info *_prev=(struct _thread_info*)prev;
        struct _thread_info *_next=(struct _thread_info*)next;

        KeReleaseSemaphore(&_next->sched_sem, 0, 1, 0);
        KeWaitForSingleObject(&_prev->sched_sem, Executive, KernelMode, FALSE, NULL);
	    if (_prev->dead) {
		   ExFreePool(_prev);
		debug_thread_count--;
		if(debug_thread_count == 0)
           KeSetEvent(&debug_thread_event,IO_NO_INCREMENT , FALSE);
		PsTerminateSystemThread(0);
	}
	
}

KSEMAPHORE kth_sem;

void DDKAPI kernel_thread_helper(void *arg)
{
        struct kernel_thread_helper_arg *ktha=(struct kernel_thread_helper_arg*)arg;
        int (*fn)(void*)=ktha->fn;
        void *farg=ktha->arg;
        struct _thread_info *pti=ktha->pti;

        KeReleaseSemaphore(&kth_sem, 0, 1, 0);
        KeWaitForSingleObject(&pti->sched_sem, Executive, KernelMode, FALSE, NULL);
        fn(farg);
}


void linux_free_thread(void *arg)
{
        struct _thread_info *pti=(struct _thread_info*)arg;

	pti->dead=1;
	KeReleaseSemaphore(&pti->sched_sem, 0, 1, 0);
}

int linux_new_thread(int (*fn)(void*), void *arg, void *pti)
{
        struct kernel_thread_helper_arg ktha = {
                .fn = fn,
                .arg = arg,
                .pti = (struct _thread_info*)pti
        };

	debug_thread_count++;
	PsCreateSystemThread(&ktha.pti->th, THREAD_ALL_ACCESS, NULL, NULL, NULL,
			     kernel_thread_helper, &ktha);
	KeWaitForSingleObject(&kth_sem, Executive, KernelMode, FALSE, NULL);
        return 0;
}

long linux_panic_blink(long time)
{
    DbgPrint("***Kernel panic!***");
	while (1);
        return 0;
}

static void *_phys_mem;

void linux_mem_init(unsigned long *phys_mem, unsigned long *phys_mem_size)
{
        *phys_mem_size=32*1024*1024;
        _phys_mem=ExAllocatePoolWithTag(NonPagedPool, *phys_mem_size, 'MNIL');
	*phys_mem=(unsigned long)_phys_mem;
	
}
static KEVENT wait_for_me;
static KEVENT good_to_go;


int linux_mount_disk(void *wdev, const char *name, const char *fs, PLINDEV lin_dev)
{
	void *ldisk;
	dev_t devno;
	char devno_str[]= { "/dev/xxxxxxxxxxxxxxxx" };
	char *mnt;
	
	if(!lin_dev)
        return STATUS_INVALID_PARAMETER;

	if (lkl_disk_add_disk(wdev, name, lklfsd.no_mounts, &devno, &ldisk)) 
		goto out_error;

	/* create /dev/dev */
	snprintf(devno_str, sizeof(devno_str), "/dev/%016x", devno);
	if (sys_mknod(devno_str, S_IFBLK|0600, devno)) 
		goto out_del_disk;

	/* create /mnt/filename */ 
	mnt=ExAllocatePool(NonPagedPool, strlen("/mnt/")+strlen(name)+1);
	if (!mnt)
		goto out_del_dev;

	sprintf(mnt, "/mnt/%s", name);
	if (sys_mkdir(mnt, 0700))
		goto out_free_mnt;

	/* mount */
	DbgPrint("Mounting %s in %s", devno_str, mnt);
	if (sys_safe_mount(devno_str, mnt, (char*)fs, 0, 0))
		goto out_del_mnt_dir;

	lin_dev->ldisk = ldisk;
	lin_dev->mnt_length = strlen(mnt);
	lin_dev->devno_str_length = sizeof(devno_str);
	RtlCopyMemory(lin_dev->mnt,mnt, lin_dev->mnt_length); 
	RtlCopyMemory(lin_dev->devno_str, devno_str, lin_dev->devno_str_length);
	ExFreePool(mnt);

	return STATUS_SUCCESS;

out_del_mnt_dir:
	sys_unlink(mnt);
out_free_mnt:
	ExFreePool(mnt);
out_del_dev:
	sys_unlink(devno_str);
out_del_disk:
	lkl_disk_del_disk(ldisk);
out_error:
	DbgPrint("can't mount disk %s\n", name);
	return STATUS_INVALID_DEVICE_REQUEST;
}


void linux_main(void)
{
     KeSetEvent(&good_to_go,IO_NO_INCREMENT , FALSE);
     sys_mkdir("/mnt", 0700);
     KeWaitForSingleObject(&wait_for_me,  Executive, KernelMode, FALSE, NULL);	
}

KSEMAPHORE idle_sem;

void linux_exit_idle(void)
{
	KeReleaseSemaphore(&idle_sem, 0, 1, 0);
}

void linux_enter_idle(int halted)
{
	LARGE_INTEGER li = {
		.QuadPart = 0,
	};

	KeWaitForSingleObject(&idle_sem, Executive, KernelMode, FALSE, halted?&li:NULL);
}

/*
 * With 64 bits, we can cover about 584 years at a nanosecond resolution. 
 * Windows counts time from 1601 (do they plan to send a computer back in time
 * and take over the world??) so we neeed to do some substractions, otherwise we
 * would overflow. 
 */
LARGE_INTEGER basetime;

unsigned long long linux_time(void)
{
	LARGE_INTEGER li;

	KeQuerySystemTime(&li);
	
        return (li.QuadPart-basetime.QuadPart)*100;
}

static KDPC timer_dpc;

void linux_timer(unsigned long delta);

static void DDKAPI timer_dpc_f(KDPC *dpc, void *arg, void *x, void *y)
{
	//FIXME: need to disable at least dpc while running irqs so that the
	//enqueue and dequeue operations of the irq queue don't race; we
	//probably need to introduce new irq disable/enable ops 
	linux_trigger_irq(TIMER_IRQ);
}

KTIMER timer;

void linux_timer(unsigned long delta)
{
	if (!delta)
		KeCancelTimer(&timer);
	else
		KeSetTimer(&timer, RtlConvertLongToLargeInteger((unsigned long)(-(delta/100))), &timer_dpc);
}


void linux_halt(void)
{
	ExFreePool(_phys_mem);
   
}


static struct linux_native_operations lnops = {
	.panic_blink = linux_panic_blink,
	.mem_init = linux_mem_init,
	.main = linux_main,
	.halt = linux_halt,
	.thread_info_alloc = linux_thread_info_alloc,
	.new_thread = linux_new_thread,
	.free_thread = linux_free_thread,
	.context_switch = linux_context_switch,
	.enter_idle = linux_enter_idle,
	.exit_idle = linux_exit_idle,
	.timer = linux_timer,
	.time = linux_time,
};


HANDLE lith;

void DDKAPI linux_idle_thread(void *arg)
{
	linux_start_kernel(&lnops, "");
}


VOID unload_linux_kernel(void)
{
     KeSetEvent(&wait_for_me, IO_NO_INCREMENT, FALSE);
     
     KeWaitForSingleObject(&debug_thread_event, Executive, KernelMode, FALSE, NULL);
}

NTSTATUS run_linux_kernel(void)
{
	NTSTATUS status=STATUS_SUCCESS;
	
	KeInitializeSemaphore(&kth_sem, 0, 100);	

	KeInitializeSemaphore(&idle_sem, 0, 100);	

	KeInitializeDpc(&timer_dpc, timer_dpc_f, NULL);
	KeInitializeTimer(&timer);
	
	KeQuerySystemTime(&basetime);

    KeInitializeEvent(&debug_thread_event, NotificationEvent, FALSE);
    KeInitializeEvent(&wait_for_me, NotificationEvent, FALSE);
    KeInitializeEvent(&good_to_go, NotificationEvent, FALSE);
     
	status = PsCreateSystemThread(&lith, THREAD_ALL_ACCESS, NULL, NULL, NULL,
			     linux_idle_thread, NULL);
    KeWaitForSingleObject(&good_to_go, Executive, KernelMode, FALSE, NULL);

	return status;

}
