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
}Dev;

Dev *dev;
static void blkdrv_transfer(Dev *dev,sector_t sector,unsigned long nsector,char *buffer,int write){
	unsigned long offset=sector*logical_block_size;
	unsigned long nbytes=nsector*logical_block_size;
	if((offset+nbytes) > dev->size){
		pr_notice("Beyond-end write (%ld %ld)\n",offset,nbytes);
		return;
	}
	if(write)
		memcpy(dev->data+offset,buffer,nbytes);
	else
		memcpy(buffer,dev->data+offset,nbytes);

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
	req=blk_fetch_request(q);
	while(req!=NULL){
		if(req==NULL && req->cmd_type!=REQ_TYPE_FS){
			pr_err("%s: Request type is not FS(REQ_TYPE_FS) type\n",__func__);
			__blk_end_request_all(req,-EIO);
			continue;
		}
		blkdrv_transfer(dev,blk_rq_pos(req),blk_rq_cur_sectors(req),req->buffer,rq_data_dir(req));
		if(!__blk_end_request_cur(req,0)){
			req=blk_fetch_request(q);
		}
	}
	
}
static int blkdrv_getgeo(struct block_device *blk_dev, struct hd_geometry *geo){
	long size;
	size=dev->size *(logical_block_size / KERNEL_SECTOR_SIZE);
	geo->cylinders= (size & ~0x3f) >> 6 ;
	geo->heads= 4;
	geo->sectors=16;
	geo->start=4;
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
	.getgeo  = blkdrv_getgeo,
	.open    = blkdrv_open,
	.release = blkdrv_release,
};

static int blkdrv_init(void){
	pr_info("%s: Initialization of Block device driver\n",__func__);
	dev=kmalloc(sizeof(struct blk_dev),GFP_KERNEL);
	dev->size=logical_block_size*nsector;
	pr_info("size =%d ,cyl=geo->cylinders %d\n",dev->size,((dev->size & ~(0x3f))>>6));
	spin_lock_init(&dev->lock);
	dev->data=vmalloc(dev->size);
	if(!dev->data){
		pr_err("%s: Vmalloc allocation failed\n",__func__);
		return -ENOMEM;
	}
	dev->Queue=blk_init_queue(blkdrv_req,&dev->lock);
	if(!dev->Queue){
		pr_err("blk_init_queue: Queue Initialization Failed\n");
		goto free;
	}
	blk_queue_logical_block_size(dev->Queue,logical_block_size);
	/*device regidtration*/
	majornumber=register_blkdev(0,DEVICE_NAME);
	if(majornumber < 0){
		pr_err("%s: BLOCK device registeration failed\n",__func__);
		goto free;
	}
	dev->gd=alloc_disk(MINOR_NO);
	if(!dev->gd){
		pr_err("GENDISK: alloc_disc Allocation failed\n");
		goto unregister;
	}
	dev->gd->major=majornumber;
	dev->gd->first_minor=0;
	dev->gd->minors=2;
	dev->gd->fops=&blkdrv_fops;
	dev->gd->private_data=&dev;
	strcpy(dev->gd->disk_name,"sbd0");
	set_capacity(dev->gd,nsector);
	dev->gd->queue=dev->Queue;
	add_disk(dev->gd);
	return 0;
unregister:
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


