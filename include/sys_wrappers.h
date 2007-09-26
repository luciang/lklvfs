#ifndef _LKL_SYS_WRAP_H
#define _LKL_SYS_WRAP_H

#include <ddk/ntddk.h>
#undef FASTCALL
#include <asm/unistd.h>
#undef FASTCALL

typedef struct stat           STATS;
typedef struct stat*          PSTATS;
typedef struct statfs         STATFS;
typedef struct statfs*        PSTATFS;
typedef struct linux_dirent   DIRENT;
typedef struct linux_dirent*  PDIRENT;

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

#endif
