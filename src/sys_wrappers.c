/**
* implement this at your will
**/

#include <sys_wrappers.h>

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

LONG sys_unmount_wrapper(PLINDEV ldev)
{
     LONG rc = 0;
     
     if(!ldev)
         return -1;
     sys_sync();
     rc = sys_umount(ldev->mnt, 0x00000001);
     if(rc <0)
           return rc;

    sys_unlink(ldev->mnt);
	sys_unlink(ldev->devno_str);
	lkl_disk_del_disk(ldev->ldisk);
	
	return rc;
}
