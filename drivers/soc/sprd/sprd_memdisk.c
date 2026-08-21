#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/kernel.h>	/* pr_notice() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

MODULE_LICENSE("Dual BSD/GPL");

static int memdisk_major = 0;
module_param(memdisk_major, int, 0);
static int hardsect_size = 512;
module_param(hardsect_size, int, 0);

/*
 * We can tweak our hardware sector size, but the kernel talks to us
 * in terms of small sectors, always.
 */
#define KERNEL_SECTOR_SIZE 512
#define MEMDISK_MINORS	16
/*
 * The internal representation of our device.
 */
struct memdisk_dev {
	unsigned long size;	/* Device size in sectors */
	spinlock_t lock;	/* For mutual exclusion */
	u8 *data;		/* The data array */
	struct request_queue *queue;	/* The device request queue */
	struct gendisk *gd;	/* The gendisk structure */
};
static int memdisks_count = 0;
static struct memdisk_dev *memdisks = NULL;

static void memdisk_submit_bio(struct bio *bio)
{
	struct memdisk_dev *dev = bio->bi_bdev->bd_disk->private_data;
	bool write = op_is_write(bio->bi_opf);

	do {
		struct bio_vec bv = bio_iter_iovec(bio, bio->bi_iter);
		unsigned long offset = bio->bi_iter.bi_sector * hardsect_size;
		unsigned long nbytes = bv.bv_len;
		void *kaddr;

		if ((offset + nbytes) > (dev->size * hardsect_size)) {
			pr_notice("memdisk: Beyond-end write (%ld %ld)\n",
				  offset, nbytes);
			bio_io_error(bio);
			return;
		}

		kaddr = bvec_kmap_local(&bv);
		if (write)
			memcpy(dev->data + offset, kaddr, nbytes);
		else
			memcpy(kaddr, dev->data + offset, nbytes);
		kunmap_local(kaddr);

		bio_advance_iter_single(bio, &bio->bi_iter, bv.bv_len);
	} while (bio->bi_iter.bi_size);

	bio_endio(bio);
}

/*
 * The HDIO_GETGEO ioctl is handled in blkdev_ioctl(), i
 * calls this. We need to implement getgeo, since we can't
 * use tools such as fdisk to partition the drive otherwise.
 */
static int memdisk_getgeo(struct gendisk *disk, struct hd_geometry *geo)
{
	long size;
	struct memdisk_dev *dev = disk->private_data;

	pr_notice("memdisk_getgeo. \n");
	size = dev->size * (hardsect_size / KERNEL_SECTOR_SIZE);
	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 4;

	return 0;
}

/*
 * The device operations structure.
 */
static struct block_device_operations memdisk_ops = {
	.owner = THIS_MODULE,
	.submit_bio = memdisk_submit_bio,
	.getgeo = memdisk_getgeo
};

/*
 * Set up our internal device.
 */
static void memdisk_setup_device(struct memdisk_dev *dev, int i)
{
	pr_notice("memdisk_setup_device i:%d. base is 0x%lx, size is 0x%lx\n",
		  i, (unsigned long)dev->data, dev->size);

	spin_lock_init(&dev->lock);

	dev->gd = blk_alloc_disk(NULL, NUMA_NO_NODE);
	if (!dev->gd) {
		pr_notice("memdisk_setup_device blk_alloc_disk failure. \n");
		return;
	}
	dev->queue = dev->gd->queue;
	dev->gd->fops = &memdisk_ops;
	dev->gd->private_data = dev;

	snprintf(dev->gd->disk_name, 32, "memdisk.%d", i);
	set_capacity(dev->gd,
		     (sector_t)(memdisks[i].size * (hardsect_size / KERNEL_SECTOR_SIZE)));
	if (add_disk(dev->gd)) {
		pr_notice("memdisk_setup_device add_disk failure. \n");
		put_disk(dev->gd);
		dev->gd = NULL;
		return;
	}

	pr_notice("memdisk_setup_device i:%d success.\n", i);
	return;
}

static void *memdisk_ram_vmap(phys_addr_t start, size_t size,
				 unsigned int memtype)
{
	struct page **pages;
	phys_addr_t page_start;
	unsigned int page_count;
	pgprot_t prot;
	unsigned int i;
	void *vaddr;

	page_start = start - offset_in_page(start);
	page_count = DIV_ROUND_UP(size + offset_in_page(start), PAGE_SIZE);

	if (memtype)
		prot = pgprot_noncached(PAGE_KERNEL);
	else
		prot = pgprot_writecombine(PAGE_KERNEL);

	pages = kmalloc_array(page_count, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		pr_err("%s: Failed to allocate array for %u pages\n",
		       __func__, page_count);
		return NULL;
	}

	for (i = 0; i < page_count; i++) {
		phys_addr_t addr = page_start + i * PAGE_SIZE;
		pages[i] = pfn_to_page(addr >> PAGE_SHIFT);
	}
	vaddr = vm_map_ram(pages, page_count, -1);
	kfree(pages);

	return vaddr;
}

static int memdisk_init(void)
{
	int i = 0;
	int ret = 0;
	struct resource res = { 0 };
	struct device_node *np = NULL;
	struct device_node *memnp = NULL;

	pr_notice("sprd memdisk init \n");

	np = of_find_compatible_node(NULL, NULL, "sprd,memdisk");
	if (!np)
		return -ENODEV;
	memnp = of_parse_phandle(np, "memory-region", 0);
	if (!memnp)
		return -ENODEV;

	memdisks_count = of_property_count_u32_elems(memnp, "reg");
	memdisks_count =
	    memdisks_count / (of_n_addr_cells(memnp) + of_n_size_cells(memnp));
	pr_notice("sprd memdisk count is %d\n", memdisks_count);
	memdisks =
	    kmalloc(sizeof(struct memdisk_dev) * memdisks_count, GFP_KERNEL);
	if (!memdisks)
		return -ENOMEM;

	for (i = 0; i < memdisks_count; i++) {
		ret = of_address_to_resource(memnp, i, &res);
		if (0 != ret) {
			pr_notice("of_address_to_resource failed!\n");
			return -EINVAL;
		}

#ifdef CONFIG_64BIT
		pr_notice("memdisk %d res start 0x%llx,end 0x%llx\n", i,
			  res.start, res.end);
#else
		pr_notice("memdisk %d res start 0x%x,end 0x%x\n", i,
			  res.start, res.end);
#endif
		memdisks[i].data =
		    memdisk_ram_vmap(res.start, resource_size(&res), 0);
		if (!memdisks[i].data) {
			pr_notice("sprd memdisk%d map error!\n", i);
			return -ENOMEM;
		}
		memdisks[i].size = resource_size(&res) / hardsect_size;
		memdisk_setup_device(&memdisks[i], i);
	}

	of_node_put(np);
	of_node_put(memnp);

	pr_notice("memdisk_init finished. \n");
	return 0;
}

static void memdisk_exit(void)
{
	int i;

	pr_notice("memdisk_exit. \n");
	for (i = 0; i < memdisks_count; i++) {
		struct memdisk_dev *dev = &memdisks[i];

		if (dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
	}
	unregister_blkdev(memdisk_major, "memdisk");
}

module_init(memdisk_init);
module_exit(memdisk_exit);
