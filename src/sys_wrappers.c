/**
* implement this at your will
**/
#include <asm/lkl.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <asm/unistd.h>
#include <asm/disk.h>
#include <sys_wrappers.h>
#include <linux/fs.h>

#include <lklvfs.h>


extern int snprintf(char * buf, size_t size, const char * fmt, ...)
	__attribute__ ((format (printf, 3, 4)));
extern int sprintf(char * buf, const char * fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

static ERESOURCE barier;

NTSTATUS InitializeSysWrappers()
{
	// do whatever kind of initialization here
	ExInitializeResourceLite(&barier);        
	return STATUS_SUCCESS;
}

void FreeSysWrapperResources()
{
	// free all used resources here
	ExDeleteResourceLite(&barier);
}

LONG sys_open_wrapper(PCSTR pathName, INT flags, INT mode)
{
	LONG rc;

        ExAcquireResourceExclusiveLite(&barier,TRUE);
	rc = lkl_sys_open(pathName, flags, mode);
	RELEASE(&barier);

	return rc;
}

LONG sys_close_wrapper(UINT fd)
{
	LONG rc;

	ExAcquireResourceExclusiveLite(&barier,TRUE);
	rc = lkl_sys_close(fd);
	RELEASE(&barier);

	return rc;
}

LONG sys_read_wrapper(UINT fd, IN PVOID buf, ULONG size)
{
	LONG rc;

	ExAcquireResourceExclusiveLite(&barier, TRUE);
	rc = lkl_sys_read(fd, (char*) buf, size);
	RELEASE(&barier);

	return rc;
}

LONG sys_lseek_wrapper(UINT fd, __kernel_off_t offset, UINT origin)
{
	LONG rc;
	
	ExAcquireResourceExclusiveLite(&barier, TRUE);
	rc = lkl_sys_lseek(fd, offset, origin);
	RELEASE(&barier);

	return rc;
}

LONG sys_newfstat_wrapper(UINT fd, OUT PSTATS stat_buff)
{
	LONG rc;

	ExAcquireResourceExclusiveLite(&barier, TRUE);
	rc = lkl_sys_newfstat(fd, stat_buff);
	RELEASE(&barier);

	return rc;
}

LONG sys_newstat_wrapper(IN PSTR filename,OUT PSTATS statbuf)
{
	LONG rc;

	ExAcquireResourceExclusiveLite(&barier, TRUE);
	rc = lkl_sys_newstat(filename, statbuf);
	RELEASE(&barier);

	return rc;
}

LONG sys_statfs_wrapper(PCSTR path, OUT PSTATFS statfs_buff)
{
	LONG rc;

	ExAcquireResourceExclusiveLite(&barier, TRUE);
	rc = lkl_sys_statfs(path, statfs_buff);
	RELEASE(&barier);

	return rc;
}

LONG sys_getdents_wrapper(UINT fd, OUT PDIRENT dirent, UINT count)
{
	LONG rc;

	ExAcquireResourceExclusiveLite(&barier, TRUE);
	rc = lkl_sys_getdents(fd, dirent, count);
	RELEASE(&barier);

	return rc;
}


int get_filesystem_list(char * buf);


static void get_fs_names(char *page)
{
	char *s = page;
	int len = get_filesystem_list(page);
	char *p, *next;
	
	page[len] = '\0';
	for (p = page-1; p; p = next) {
		next = strchr(++p, '\n');
		if (*p++ != '\t')
			continue;
		while ((*s++ = *p++) != '\n')
			;
		s[-1] = '\0';
	}
	
	*s = '\0';
}

/*
 * Try to mount with all filesystems. 
 */
static int try_mount(char *devno_str, char *mnt, int flags, void *data)
{
	int err;
	char *p, *fs_names=ExAllocatePool(PagedPool, PAGE_SIZE);
	
	get_fs_names(fs_names);
retry:
	for (p = fs_names; *p; p += strlen(p)+1) {
		DbgPrint("trying %s\n", p);
		err = lkl_sys_mount(devno_str, mnt, p, flags, data);
		switch (err) {
			case 0:
				goto out;
			case -EACCES:
				flags |= MS_RDONLY;
				goto retry;
			case -EINVAL:
				continue;
		}
	}
out:
	ExFreePool(fs_names);

	return err;
}

LONG sys_mount_wrapper(void *wdev, int sectors, PLINDEV lin_dev)
{
	__kernel_dev_t devno;
	char devno_str[]= { "/dev/xxxxxxxxxxxxxxxx" };
	char *mnt;
	char *name = "sys_mount_wrapper__name_not_initialized";

	if(!lin_dev)
	        return STATUS_INVALID_PARAMETER;

	devno = lkl_disk_add_disk(wdev, sectors);
	if (devno == 0)
		goto out_error;

	/* create /dev/dev */
	snprintf(devno_str, sizeof(devno_str), "/dev/%016x", devno);
	if (lkl_sys_mknod(devno_str, S_IFBLK|0600, devno) != 0)
		goto out_del_disk;

	name = devno_str + strlen("/dev/");
	/* create /mnt/filename */ 
	mnt=ExAllocatePool(NonPagedPool, strlen("/mnt/")+strlen(name)+1);
	if (!mnt)
		goto out_del_dev;

	sprintf(mnt, "/mnt/%s", name);
	if (lkl_sys_mkdir(mnt, 0700))
		goto out_free_mnt;

	DbgPrint("Mounting %s in %s", devno_str, mnt);
	if (try_mount(devno_str, mnt, MS_RDONLY, 0))
		goto out_del_mnt_dir;

	lin_dev->devno = devno;
	lin_dev->mnt_length = strlen(mnt);
	lin_dev->devno_str_length = sizeof(devno_str);
	RtlCopyMemory(lin_dev->mnt,mnt, lin_dev->mnt_length); 
	RtlCopyMemory(lin_dev->devno_str, devno_str, lin_dev->devno_str_length);
	ExFreePool(mnt);

	return STATUS_SUCCESS;

out_del_mnt_dir:
	lkl_sys_unlink(mnt);
out_free_mnt:
	ExFreePool(mnt);
out_del_dev:
	lkl_sys_unlink(devno_str);
out_del_disk:
	lkl_disk_del_disk(devno);
out_error:
	DbgPrint("can't mount disk %s\n", name);
	return STATUS_INVALID_DEVICE_REQUEST;
}


LONG sys_unmount_wrapper(PLINDEV ldev)
{
     LONG rc = 0;
     
     if(!ldev)
         return -1;

     rc = lkl_sys_umount(ldev->mnt, 0);
     if(rc <0)
           return rc;

     lkl_sys_unlink(ldev->mnt);
     lkl_sys_unlink(ldev->devno_str);
     lkl_disk_del_disk(ldev->devno);
     
     return rc;
}

void sys_sync_wrapper()
{
     lkl_sys_sync();
}
