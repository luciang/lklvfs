/**
* implement this at your will
**/
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <asm/unistd.h>
#include <drivers/disk.h>
#include <sys_wrappers.h>
#include <linux/fs.h>

#include <lklvfs.h>

extern int snprintf(char * buf, size_t size, const char * fmt, ...)
	__attribute__ ((format (printf, 3, 4)));
extern int sprintf(char * buf, const char * fmt, ...)
	__attribute__ ((format (printf, 2, 3)));


NTSTATUS InitializeSysWrappers()
{
         // do whatever kind of initialization here
         
         return STATUS_SUCCESS;
}

void FreeSysWrapperResources()
{
     // free all used resources here

}

LONG sys_open_wrapper(PCSTR pathName, INT flags, INT mode)
{
     LONG rc;
     
     rc = sys_open(pathName, flags, mode);
     
     return rc;
}

LONG sys_close_wrapper(UINT fd)
{
     LONG rc;
     
     rc = sys_close(fd);
     
     return rc;
}

LONG sys_read_wrapper(UINT fd, IN PVOID buf, ULONG size)
{
      LONG rc;
      
      rc = sys_read(fd, (char*) buf, size);
      
      return rc;
}

LONG sys_lseek_wrapper(UINT fd, off_t offset, UINT origin)
{
     LONG rc;
     
     rc = sys_lseek(fd, offset, origin);
     
     return rc;
}

LONG sys_newfstat_wrapper(UINT fd, OUT PSTATS stat_buff)
{
     LONG rc;
     
     rc = sys_newfstat(fd, stat_buff);
     
     return rc;
}

LONG sys_newstat_wrapper(IN PSTR filename,OUT PSTATS statbuf)
{
     LONG rc;
     
     rc = sys_newstat(filename, statbuf);
     
     return rc;
}

LONG sys_statfs_wrapper(PCSTR path, OUT PSTATFS statfs_buff)
{
     LONG rc;
     
     rc = sys_statfs(path, statfs_buff);
   
     return rc;
}

LONG sys_getdents_wrapper(UINT fd, OUT PDIRENT dirent, UINT count)
{
     LONG rc;
     
     rc = sys_getdents(fd, dirent, count);
     
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
		err = sys_safe_mount(devno_str, mnt, p, flags, data);
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

LONG sys_mount_wrapper(void *wdev, const char *name, PLINDEV lin_dev)
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

	DbgPrint("Mounting %s in %s", devno_str, mnt);
	if (try_mount(devno_str, mnt, 0, 0))
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


LONG sys_unmount_wrapper(PLINDEV ldev)
{
     LONG rc = 0;
     
     if(!ldev)
         return -1;

     rc = sys_umount(ldev->mnt, 0x00000002);
     if(rc <0)
           return rc;

    sys_unlink(ldev->mnt);
	sys_unlink(ldev->devno_str);
	lkl_disk_del_disk(ldev->ldisk);
	
	return rc;
}

void sys_sync_wrapper()
{
     sys_sync();
}
