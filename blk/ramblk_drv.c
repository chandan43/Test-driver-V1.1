#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/hdreg.h>
#include <linux/kernel.h>
#include <linux/version.h>

#include "ram.h"

#define DEVICE_NAME "blk_drv"
#define KERNEL_SECTOR_SIZE 512
#define MINOR_NO 16


static int majornumber=0;
module_param(majornumber,int,0);
MODULE_PARM_DESC(majornumber,"MAJOR NO. : Major number of logical block device driver.");
static int logical_block_size=512;
module_param(logical_block_size,int,0);
MODULE_PARM_DESC(logical_block_size,"SECTOR SIZE");
static int nsector=1024;
module_param(nsector,int,0);
MODULE_PARM_DESC(nsector,"Total device size:  nsector * logical_block_size");


/*Internal structure of Virtual block device*/
typedef struct blk_dev{
	int size;  
	u8 *data;
	struct gendisk *gd;
	struct request_queue *Queue;
	spinlock_t lock;
//	u8 *buffer;
}Dev;

Dev *dev;
static void copy_mbr(u8 *disk){
	memset(disk, 0x0, MBR_SIZE);
	*(unsigned long *)(disk + MBR_DISK_SIGNATURE_OFFSET) = 0x36E5756D;   /*Disk identifier*/
	memcpy(disk + PARTITION_TABLE_OFFSET, &deff_partition_table, PARTITION_TABLE_SIZE);
	*(unsigned short *)(disk + MBR_SIGNATURE_OFFSET) = MBR_SIGNATURE;   /*Partition sector have the endmark MBR_SIGNATURE(0xAA55) */
}
//static int blkdrv_transfer(struct request *req,sector_t start_sector,unsigned long sector_cnt,u8 *buffer,int direction){
static int blkdrv_transfer(struct request *req){
	sector_t start_sector=blk_rq_pos(req);
	unsigned int sector_cnt = blk_rq_sectors(req);
	int direction=rq_data_dir(req);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)) 
#define  BV_PAGE(bv) ((bv)->bv_page)
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
	static unsigned int sectors;
	sector_t offset;
	unsigned long nbytes;
	static int ret;
	u8 *buffer;
	sector_t sector_offset;
	sector_offset=0;
	rq_for_each_segment(bv, req, iter){
		buffer=page_address(BV_PAGE(bv) + BV_OFFSET(bv));
		if( BV_LEN(bv) % logical_block_size !=0){
			pr_err("%s: Should never happen: bio size (%d) is not a multiple of ''logical_block_size'' (%d)\n",__func__,BV_LEN(bv),logical_block_size);
			ret = -EIO;
		}
		sectors = BV_LEN(bv)/logical_block_size;
		pr_info("%s: Start Sector: %llu, Sector Offset: %llu; Buffer: %p; Length: %u sectors\n",__func__,
				(unsigned long long)(start_sector), (unsigned long long)(sector_offset), buffer, sectors);

		offset= (start_sector + sector_offset);
		nbytes= sectors *logical_block_size;
		if(direction)
			memcpy(dev->data + offset * logical_block_size ,buffer,nbytes);
		else
			memcpy(buffer,dev->data + offset * logical_block_size ,nbytes);
		sector_offset += sectors;
	}
	if(sector_offset != sector_cnt){
		pr_err("%s: Bio info doesn't match with the request info\n",__func__);
		ret = -EIO;
	}
	pr_info("ret value===%d\n",ret);
	return ret;
}
/*
 * blk_rq_pos()                 : the current sector
 * blk_rq_bytes()               : bytes left in the entire request
 * blk_rq_cur_bytes()           : bytes left in the current segment
 * blk_rq_err_bytes()           : bytes left till the next error boundary
 * blk_rq_sectors()             : sectors left in the entire request
 * blk_rq_cur_sectors()         : sectors left in the current segment
 */
static void blkdrv_req(struct request_queue *q){
	struct request *req;
	int ret;
	while((req=blk_fetch_request(q)) != NULL){
	/*(	if(req==NULL && req->cmd_type!=REQ_TYPE_FS){
			pr_err("%s: Request type is not FS(REQ_TYPE_FS) type\n",__func__);
			__blk_end_request_all(req,-EIO);
			continue;
		}*/
	//	ret=blkdrv_transfer(req,blk_rq_pos(req),blk_rq_sectors(req),dev->buffer,rq_data_dir(req));
		ret=blkdrv_transfer(req);
		__blk_end_request_all(req,ret);
/*		if(!__blk_end_request_cur(req,0)){
		//	req=blk_fetch_request(q);
//		__blk_end_request(req, ret, blk_rq_bytes(req));
		//	__blk_end_request_all(req,ret);
		}*/
	}
	
}
static int blkdrv_getgeo(struct block_device *blk_dev, struct hd_geometry *geo){
	geo->heads= 1;
	geo->cylinders= 32 ;
	geo->sectors=32;
	geo->start=0;
	return 0;
}
static int blkdrv_open(struct block_device *blk_dev, fmode_t mode){
	unsigned int unit = iminor(blk_dev->bd_inode);
	pr_info("Block Device: Device is opened\n");
	pr_info("Inode number is %d\n", unit);
	if(unit > MINOR_NO)
		return ENODEV;
	return 0;
}
static void blkdrv_release(struct gendisk *gd, fmode_t mode){
	pr_info("%s: Block Device is closed\n",__func__);
}
static const struct block_device_operations blkdrv_fops = {
	.owner   = THIS_MODULE,
	.open    = blkdrv_open,
	.release = blkdrv_release,
	.getgeo  = blkdrv_getgeo,
};

static int blkdrv_init(void){
	pr_info("%s: Initialization of Block device driver\n",__func__);
	dev=kmalloc(sizeof(struct blk_dev),GFP_KERNEL);
	dev->size=logical_block_size*nsector;
	spin_lock_init(&dev->lock);
	dev->data=vmalloc(dev->size);
	if(!dev->data){
		pr_err("%s: Vmalloc allocation failed\n",__func__);
		return -ENOMEM;
	}
	copy_mbr(dev->data);                                                                           /*Copy disk partition table*/
	/*device regidtration*/
	majornumber=register_blkdev(0,DEVICE_NAME);
	if(majornumber < 0){
		pr_err("%s: BLOCK device registeration failed\n",__func__);
		goto free;
	}
	dev->Queue=blk_init_queue(blkdrv_req,&dev->lock);
	if(!dev->Queue){
		pr_err("blk_init_queue: Queue Initialization Failed\n");
		goto free;
	}
//	blk_queue_logical_block_size(dev->Queue,logical_block_size);
	dev->gd=alloc_disk(MINOR_NO);
	if(!dev->gd){
		pr_err("GENDISK: alloc_disc Allocation failed\n");
		goto unregister;
	}
	dev->gd->major=majornumber;
//	dev->gd->first_minor=0;
	dev->gd->minors=16;
	dev->gd->fops=&blkdrv_fops;
	dev->gd->private_data=&dev;
	dev->gd->queue=dev->Queue;
	strcpy(dev->gd->disk_name,"vd");
	set_capacity(dev->gd,nsector);
	add_disk(dev->gd);
	pr_info(": Ram Block driver initialised (%d sectors; %d bytes)\n",
		nsector, dev->size);
	return 0;
unregister:
	blk_cleanup_queue(dev->Queue);
	unregister_blkdev(majornumber,DEVICE_NAME);
free:
	vfree(dev->data);
	kfree(dev);
	return 0;
}

static void __exit blkdrv_exit(void){
	del_gendisk(dev->gd);
	put_disk(dev->gd);
	unregister_blkdev(majornumber,DEVICE_NAME);
	blk_cleanup_queue(dev->Queue);
	vfree(dev->data);
	kfree(dev);
	pr_info("%s: Exited Successfully\n",__func__);
}



module_init(blkdrv_init);
module_exit(blkdrv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_VERSION(".1");
MODULE_DESCRIPTION("Virtual Block Device Driver");


