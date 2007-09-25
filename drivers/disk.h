#ifndef _FILE_DISK_H
#define _FILE_DISK_H

#define LKL_DISK_CS_ERROR 0
#define LKL_DISK_CS_SUCCESS 1

#ifdef LKL_DISK_ASYNC
#define LKL_DISK_IRQ 1
struct lkl_disk_cs {
	void *linux_cookie;
	int status;
};
void lkl_disk_do_rw(void *f, unsigned long sector, unsigned long nsect,
		    char *buffer, int dir, struct lkl_disk_cs *cs);
#else
int lkl_disk_do_rw(void *f, unsigned long sector, unsigned long nsect,
		    char *buffer, int dir);
#endif

void* lkl_disk_do_open(const char *filename);
unsigned long lkl_disk_get_sectors(void*);
int lkl_disk_add_disk(void *wdev, const char *name, int which, dev_t *devno,
		      void **gd);
void lkl_disk_del_disk(void *_gd);

#endif
