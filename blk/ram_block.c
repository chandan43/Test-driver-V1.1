/*
 * Block driver 
 *
 * This driver is based on  3.10.X kernel. 
 * but has been rewritten to be easier to read and use.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/genhd.h> 
#include <linux/blkdev.h> 
#include <linux/hdreg.h> 
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include "partition.h"

#define FIRST_MINOR 0
#define MINOR_CNT 16
#define DEVICE_SIZE 1024 /* sectors */
/* So, total device size = 1024 * 512 bytes = 512 KiB */
#define SECTOR_SIZE 512

static u_int majornumber = 0;
int i; 

typedef struct rb_device
{
	unsigned int size;
	u8 *data;
	struct gendisk *gd;
	struct request_queue *Queue;
	spinlock_t lock;
	u8 *buffer;
}Dev;

Dev *dev;

static void copy_mbr(u8 *disk)
{
	memset(disk, 0x0, MBR_SIZE);
	*(unsigned long *)(disk + MBR_DISK_SIGNATURE_OFFSET) = 0x36E5756D;
	memcpy(disk + PARTITION_TABLE_OFFSET, &def_part_table, PARTITION_TABLE_SIZE);
	*(unsigned short *)(disk + MBR_SIGNATURE_OFFSET) = MBR_SIGNATURE;
}

static int blkdrv_open(struct block_device *bdev, fmode_t mode)
{
	unsigned unit = iminor(bdev->bd_inode);

	pr_info("blk_drv: Device is opened\n");
	pr_info("blk_drv: Inode number is %d\n", unit);

	if (unit > MINOR_CNT)
		return -ENODEV;
	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
static int blkdrv_close(struct gendisk *disk, fmode_t mode)
{
	pr_info("blk_drv: Device is closed\n");
	return 0;
}
#else
static void blkdrv_close(struct gendisk *disk, fmode_t mode)
{
	pr_info("blk_drv: Device is closed\n");
}
#endif

static int blkdrv_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->heads = 1;
	geo->cylinders = 32;
	geo->sectors = 32;
	geo->start = 0;
	return 0;
}

//static int blkdrv_transfer(struct request *req)
static int blkdrv_transfer(struct request *req,sector_t start_sector,unsigned int sector_cnt,u8 *buffer,int direction){
/*	int direction = rq_data_dir(req);
	sector_t start_sector = blk_rq_pos(req);
	unsigned int sector_cnt = blk_rq_sectors(req);
*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0))
#define BV_PAGE(bv) ((bv)->bv_page)
#define BV_OFFSET(bv) ((bv)->bv_offset)
#define BV_LEN(bv) ((bv)->bv_len)
	struct bio_vec *bv;
#else
#define BV_PAGE(bv) ((bv).bv_page)
#define BV_OFFSET(bv) ((bv).bv_offset)
#define BV_LEN(bv) ((bv).bv_len)
	struct bio_vec bv;
#endif
	struct req_iterator iter;

	sector_t sector_offset;
	unsigned int sectors;
//	u8 *buffer;
	sector_t offset;
	unsigned long nbytes;
	int ret = 0;

//	pr_debug("blk_drv: Dir:%d; Sec:%lld; Cnt:%d\n", dir, start_sector, sector_cnt);

	sector_offset = 0;
	rq_for_each_segment(bv, req, iter)
	{
		buffer = page_address(BV_PAGE(bv)) + BV_OFFSET(bv);
		if (BV_LEN(bv) % SECTOR_SIZE != 0){
			pr_err("blk_drv: Should never happen: " "bio size (%d) is not a multiple of SECTOR_SIZE (%d).\n"
					"This may lead to data truncation.\n",BV_LEN(bv), SECTOR_SIZE);
			ret = -EIO;
		}
		sectors = BV_LEN(bv) / SECTOR_SIZE;
		pr_info("blk_drv: Start Sector: %llu, Sector Offset: %llu; Buffer: %p; Length: %u sectors\n",
			(unsigned long long)(start_sector), (unsigned long long)(sector_offset), buffer, sectors);
		offset = (start_sector + sector_offset) * SECTOR_SIZE ;
		nbytes =  sectors * SECTOR_SIZE ;
		if(direction) /* Write to the device */
			memcpy(dev->data + offset,buffer,nbytes);
		else /* Read from the device */
			memcpy(buffer,dev->data + offset,nbytes);
		sector_offset += sectors;
	}
	if (sector_offset != sector_cnt){
		pr_err("blk_drv: bio info doesn't match with the request info");
		ret = -EIO;
	}

	return ret;
}
static void blkdrv_request(struct request_queue *q){
	struct request *req;
	int ret;
	while((req = blk_fetch_request(q) ) != NULL){
		if(req==NULL && req->cmd_type!=REQ_TYPE_FS){
			pr_err("%s: Request type is not FS(REQ_TYPE_FS) type\n",__func__);
			__blk_end_request_all(req,-EIO);
			continue;
		}
		ret=blkdrv_transfer(req,blk_rq_pos(req),blk_rq_sectors(req),dev->buffer,rq_data_dir(req));
//		ret=blkdrv_transfer(req);
		__blk_end_request_all(req,ret);
	}
}
static struct block_device_operations blkdrv_fops =
{
	.owner = THIS_MODULE,
	.open = blkdrv_open,
	.release = blkdrv_close,
	.getgeo = blkdrv_getgeo,
};
	
static int __init blkdrv_init(void)
{
	pr_info("%s: Initialization of Block device driver\n",__func__);
	dev=kmalloc(sizeof(struct rb_device),GFP_KERNEL);
	dev->size=DEVICE_SIZE * SECTOR_SIZE;
	spin_lock_init(&dev->lock);
	dev->data=vmalloc(dev->size);
	if(!dev->data){
		pr_err("%s: Vmalloc allocation failed\n",__func__);
		return -ENOMEM;
	}
	copy_mbr(dev->data);                                         /* Setup its partition table */
	/* Get a request queue (here queue is created) */
	dev->Queue = blk_init_queue(blkdrv_request, &dev->lock);
	if (!dev->Queue){
		pr_err("blk_init_queue: Queue Initialization Failed\n");
		vfree(dev->data);
		return -ENOMEM;
	}
	/*allocation of Request queue for request processing */
	blk_queue_logical_block_size(dev->Queue,SECTOR_SIZE);
	/* Get Registered */
	majornumber = register_blkdev(majornumber, "blk_drv");
	if (majornumber <= 0){
		pr_err("%s: BLOCK device registeration failed\n",__func__);
		vfree(dev->data);
		return -EBUSY;
	}
	pr_info("Registered: Driver Registered with %d Major number\n",majornumber);
	/*
	 * Add the gendisk structure
	 * By using this memory allocation is involved, 
	 * the minor number we need to pass bcz the device 
	 * will support this much partitions 
	 */
	dev->gd = alloc_disk(MINOR_CNT);
	if (!dev->gd){
		pr_err("GENDISK: alloc_disc Allocation failed\n");
		blk_cleanup_queue(dev->Queue);
		unregister_blkdev(majornumber, "blk_drv");
		vfree(dev->data);
		return -ENOMEM;
	}
	dev->gd->major = majornumber;
	dev->gd->first_minor = FIRST_MINOR;
	dev->gd->fops = &blkdrv_fops;
	dev->gd->private_data = &dev;
	dev->gd->queue = dev->Queue;
	/*
	 * You do not want partition information to show up in 
	 * cat /proc/partitions set this flags
	 */
	//rb_dev.gd->flags = GENHD_FL_SUPPRESS_PARTITION_INFO;
	sprintf(dev->gd->disk_name, "vd");
	set_capacity(dev->gd, DEVICE_SIZE);
	add_disk(dev->gd);
	pr_info("blk_drv: Ram Block driver initialised (%d sectors; %d bytes)\n",
		DEVICE_SIZE, dev->size );

	return 0;
}
/*
 * This is the unregistration and uninitialization section of the ram block
 * device driver
 */
static void __exit blkdrv_cleanup(void)
{
	del_gendisk(dev->gd);
	put_disk(dev->gd);
	blk_cleanup_queue(dev->Queue);
	unregister_blkdev(majornumber, "blk_drv");
	vfree(dev->data);
}

module_init(blkdrv_init);
module_exit(blkdrv_cleanup);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_VERSION(".1");
MODULE_DESCRIPTION("Virtual Block Device Driver with MBR multiple partition");
MODULE_ALIAS_BLOCKDEV_MAJOR(majornumber);
