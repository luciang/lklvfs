#include <linux/kernel.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>	/* invalidate_bdev */
#include <linux/bio.h>
#include <linux/interrupt.h>
#include <asm/irq_regs.h>

#include "disk.h"

/*
 * The internal representation of our device.
 */
struct lkl_disk_dev {
	void *wdev;
        spinlock_t lock;                /* For mutual exclusion */
        struct request_queue *queue;    /* The device request queue */
        struct gendisk *gd;             /* The gendisk structure */
};

#ifdef LKL_DISK_ASYNC
static irqreturn_t lkl_disk_irq(int irq, void *dev_id)
{
	struct pt_regs *regs=get_irq_regs();
	struct lkl_disk_cs *cs=regs->irq_data;
	struct request *req=(struct request*)cs->linux_cookie;

	printk("%s:%d: status=%d\n", __FUNCTION__, __LINE__, cs->status);
	
	end_that_request_first(req, cs->status, req->hard_cur_sectors);
	end_that_request_last(req, cs->status);

	kfree(cs);

	return IRQ_HANDLED;
}
#endif

static void lkl_disk_request(request_queue_t *q)
{
	struct request *req;

	while ((req = elv_next_request(q)) != NULL) {
		struct lkl_disk_dev *dev = req->rq_disk->private_data;
#ifdef LKL_DISK_ASYNC
		struct lkl_disk_cs *cs;


		if (!(cs=kmalloc(sizeof(*cs), GFP_KERNEL))) {
			end_request(req, 0);
			continue;
		}
#else
		int status;
#endif

		if (! blk_fs_request(req)) {
			printk (KERN_NOTICE "Skip non-fs request\n");
			end_request(req, 0);
			continue;
		}

		blkdev_dequeue_request(req);

#ifdef LKL_DISK_ASYNC		
		cs->linux_cookie=req;
		lkl_disk_do_rw(dev->wdev, req->sector, req->current_nr_sectors,
			       req->buffer, rq_data_dir(req), cs);
#else
		status=lkl_disk_do_rw(dev->wdev, req->sector, req->current_nr_sectors,
			       req->buffer, rq_data_dir(req));
		end_that_request_first(req, status, req->hard_cur_sectors);
		end_that_request_last(req, status);
#endif
	}
}

static int lkl_disk_open(struct inode *inode, struct file *filp)
{
	struct lkl_disk_dev *dev = inode->i_bdev->bd_disk->private_data;

	filp->private_data = dev;
	return 0;
}

/*
 * The device operations structure.
 */
static struct block_device_operations lkl_disk_ops = {
	.owner           = THIS_MODULE,
	.open 	         = lkl_disk_open,
};


static int major;

/*
 * Set up our internal device.
 */
int lkl_disk_add_disk(void *wdev, const char *name, int which, dev_t *devno, void **gd)
{
	struct lkl_disk_dev *dev=kmalloc(sizeof(*dev), GFP_KERNEL);

	BUG_ON(dev == NULL);

	memset (dev, 0, sizeof(*dev));

        dev->wdev=wdev;
	BUG_ON(dev->wdev == NULL);

	spin_lock_init(&dev->lock);
	
        dev->queue = blk_init_queue(lkl_disk_request, &dev->lock);
	BUG_ON(dev->queue == NULL);

	blk_queue_hardsect_size(dev->queue, 512);
	dev->queue->queuedata = dev;

	dev->gd = alloc_disk(1);
	BUG_ON(dev->gd == NULL);

	dev->gd->major = major;
	dev->gd->first_minor = which;
	dev->gd->fops = &lkl_disk_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf (dev->gd->disk_name, 32, "%s", name);
	set_capacity(dev->gd, lkl_disk_get_sectors(dev->wdev));

	add_disk(dev->gd);

	printk("lkldisk: attached %s @ dev=%d:%d\n", dev->gd->disk_name, dev->gd->major, dev->gd->first_minor);
	*devno=new_encode_dev(MKDEV(dev->gd->major, dev->gd->first_minor));
	*gd=dev->gd;

	return 0;
}

void lkl_disk_del_disk(void *_gd)
{
	del_gendisk((struct gendisk *)_gd);
}

static int __init lkl_disk_init(void)
{
#ifdef LKL_DISK_ASYNC
	int err;

	if ((err=request_irq(LKL_DISK_IRQ, lkl_disk_irq, 0, "lkldisk", NULL))) {
		printk(KERN_ERR "lkldisk: unable to register irq %d: %d\n", LKL_DISK_IRQ, err);
		return err;
	}
#endif

	major = register_blkdev(0, "fd");
	if (major < 0) {
		printk(KERN_ERR "fd: unable to register_blkdev: %d\n", major);
#ifdef LKL_DISK_ASYNC
		free_irq(LKL_DISK_IRQ, NULL);
#endif
		return -EBUSY;
	}

	return 0;
}

late_initcall(lkl_disk_init);
