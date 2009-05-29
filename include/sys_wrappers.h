#ifndef _LKL_SYS_WRAP_H
#define _LKL_SYS_WRAP_H

#include <ddk/ntddk.h>
#undef FASTCALL
#include <asm/unistd.h>
#include <asm/statfs.h>
#include <asm/stat.h>
#include <asm/types.h>
#include <linux/dirent.h>
#undef FASTCALL

typedef struct __kernel_stat    STATS;
typedef struct __kernel_stat*   PSTATS;
typedef struct __kernel_statfs  STATFS;
typedef struct __kernel_statfs* PSTATFS;
typedef struct linux_dirent64   DIRENT;
typedef struct linux_dirent64*  PDIRENT;

// used to store all the info that we'll need for unmount
typedef struct lin_dev {
      UCHAR mnt[255];
      UCHAR devno_str[255];
      USHORT mnt_length;
      USHORT devno_str_length;
      PVOID ldisk;
} LINDEV, *PLINDEV;

NTSTATUS InitializeSysWrappers();
void FreeSysWrapperResources();

LONG sys_open_wrapper(IN PCSTR pathName, INT flags, INT mode);
LONG sys_close_wrapper(UINT fd);
LONG sys_read_wrapper(UINT fd, IN PVOID buf, ULONG size);
LONG sys_lseek_wrapper(UINT fd, off_t offset, UINT origin);
LONG sys_newfstat_wrapper(UINT fd,OUT PSTATS);
LONG sys_newstat_wrapper(IN PSTR filename,OUT PSTATS statbuf);
LONG sys_statfs_wrapper(IN PCSTR path,OUT PSTATFS);
LONG sys_getdents_wrapper(UINT fd, OUT PDIRENT, UINT count);
LONG sys_mount_wrapper(void *wdev, const char *name, PLINDEV lin_dev);
LONG sys_unmount_wrapper(PLINDEV ldev);
void sys_sync_wrapper();

#endif
