/*
 * Copyright (C) 2017 Google.
 *
 * This file is released under the GPLv2.
 *
 * Based on drivers/md/dm-verity-chromeos.c
 */

#include <linux/device-mapper.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/blkdev.h>

/* 7.2 vendor tree dropped bdev_release() from headers; re-declare it. */
void bdev_release(struct file *bdev_file);

#define DM_MSG_PREFIX "verity-avb"

/* Set via module parameters. */
static char avb_vbmeta_device[64];
static char avb_invalidate_on_error[4];

static void invalidate_vbmeta_endio(struct bio *bio)
{
	if (bio->bi_status)
		DMERR("invalidate_vbmeta_endio: error %d",
		      blk_status_to_errno(bio->bi_status));
	complete(bio->bi_private);
}

static int invalidate_vbmeta_submit(struct bio *bio,
				    struct block_device *bdev,
				    int rw, int access_last_sector,
				    struct page *page)
{
	DECLARE_COMPLETION_ONSTACK(wait);

	bio->bi_private = &wait;
	bio->bi_end_io = invalidate_vbmeta_endio;
	bio->bi_bdev = bdev;
	bio->bi_opf = rw;

	bio->bi_iter.bi_sector = 0;
	if (access_last_sector) {
		sector_t last_sector;

		last_sector = bdev_nr_sectors(bdev) - 1;
		bio->bi_iter.bi_sector = last_sector;
	}
	if (!bio_add_page(bio, page, PAGE_SIZE, 0)) {
		DMERR("invalidate_vbmeta_submit: bio_add_page error");
		return -EIO;
	}

	submit_bio(bio);
	/* Wait up to 2 seconds for completion or fail. */
	if (!wait_for_completion_timeout(&wait, msecs_to_jiffies(2000)))
		return -EIO;
	return 0;
}

static int invalidate_vbmeta(dev_t vbmeta_devt)
{
	int ret = 0;
	struct block_device *bdev;
	struct file *bdev_file = NULL;
	struct bio *bio;
	struct page *page;
	blk_mode_t dev_mode;
	/* Ensure we do synchronous unblocked I/O. We may also need
	 * sync_bdev() on completion, but it really shouldn't.
	 */
	int rw = REQ_OP_READ | REQ_SYNC;
	int access_last_sector = 0;

	DMINFO("invalidate_vbmeta: acting on device %d:%d",
	       MAJOR(vbmeta_devt), MINOR(vbmeta_devt));

	/* First we open the device for reading. */
	dev_mode = BLK_OPEN_READ;
	bdev_file = bdev_file_open_by_dev(vbmeta_devt, dev_mode,
					  invalidate_vbmeta, NULL);
	if (IS_ERR(bdev_file)) {
		DMERR("invalidate_kernel: could not open device for reading");
		ret = -ENOENT;
		goto failed_to_read;
	}
	bdev = file_bdev(bdev_file);

	bio = bio_alloc(NULL, 1, 0, GFP_NOIO);
	if (!bio) {
		ret = -ENOMEM;
		goto failed_bio_alloc;
	}

	page = alloc_page(GFP_NOIO);
	if (!page) {
		ret = -ENOMEM;
		goto failed_to_alloc_page;
	}

	access_last_sector = 0;
	ret = invalidate_vbmeta_submit(bio, bdev, rw, access_last_sector, page);
	if (ret) {
		DMERR("invalidate_vbmeta: error reading");
		goto failed_to_submit_read;
	}

	/* We have a page. Let's make sure it looks right. */
	if (memcmp("AVB0", page_address(page), 4) == 0) {
		/* Stamp it. */
		memcpy(page_address(page), "AVE0", 4);
		DMINFO("invalidate_vbmeta: found vbmeta partition");
	} else {
		/* Could be this is on a AVB footer, check. Also, since the
		 * AVB footer is in the last 64 bytes, adjust for the fact that
		 * we're dealing with 512-byte sectors.
		 */
		size_t offset = (1<<SECTOR_SHIFT) - 64;

		access_last_sector = 1;
		ret = invalidate_vbmeta_submit(bio, bdev, rw,
					       access_last_sector, page);
		if (ret) {
			DMERR("invalidate_vbmeta: error reading");
			goto failed_to_submit_read;
		}
		if (memcmp("AVBf", page_address(page) + offset, 4) != 0) {
			DMERR("invalidate_vbmeta on non-vbmeta partition");
			ret = -EINVAL;
			goto invalid_header;
		}
		/* Stamp it. */
		memcpy(page_address(page) + offset, "AVE0", 4);
		DMINFO("invalidate_vbmeta: found vbmeta footer partition");
	}

	/* Now rewrite the changed page - the block dev was being
	 * changed on read. Let's reopen here.
	 */
	bdev_release(bdev_file);
	dev_mode = BLK_OPEN_WRITE;
	bdev_file = bdev_file_open_by_dev(vbmeta_devt, dev_mode,
					  invalidate_vbmeta, NULL);
	if (IS_ERR(bdev_file)) {
		DMERR("invalidate_vbmeta: could not open device for writing");
		ret = -ENOENT;
		goto failed_to_write;
	}
	bdev = file_bdev(bdev_file);

	/* We re-use the same bio to do the write after the read. Need to reset
	 * it to initialize bio->bi_remaining.
	 */
	bio_reset(bio, bdev, rw);

	rw = REQ_OP_WRITE | REQ_SYNC;
	ret = invalidate_vbmeta_submit(bio, bdev, rw, access_last_sector, page);
	if (ret) {
		DMERR("invalidate_vbmeta: error writing");
		goto failed_to_submit_write;
	}

	DMERR("invalidate_vbmeta: completed.");
	ret = 0;
failed_to_submit_write:
failed_to_write:
invalid_header:
	__free_page(page);
failed_to_submit_read:
	/* Technically, we'll leak a page with the pending bio, but
	 * we're about to reboot anyway.
	 */
failed_to_alloc_page:
	bio_put(bio);
failed_bio_alloc:
	if (bdev_file && !IS_ERR(bdev_file))
		bdev_release(bdev_file);
failed_to_read:
	return ret;
}

/* Minimal replacement for removed name_to_dev_t(): supports "maj:min"
 * and "/dev/..." paths (partition-name style is not supported on 7.2
 * since blk_lookup_devt() was removed as well).
 */
static dev_t avb_name_to_dev_t(const char *name)
{
	dev_t dev = 0;
	unsigned int maj, min;

	if (sscanf(name, "%u:%u", &maj, &min) == 2) {
		dev = MKDEV(maj, min);
		if (maj != MAJOR(dev) || min != MINOR(dev))
			return 0;
		return dev;
	}
	if (strncmp(name, "/dev/", 5) == 0) {
		if (lookup_bdev(name, &dev) == 0)
			return dev;
	}
	return 0;
}

void dm_verity_avb_error_handler(void)
{
	dev_t dev;

	DMINFO("AVB error handler called for %s", avb_vbmeta_device);

	if (strcmp(avb_invalidate_on_error, "yes") != 0) {
		DMINFO("Not configured to invalidate");
		return;
	}

	if (avb_vbmeta_device[0] == '\0') {
		DMERR("avb_vbmeta_device parameter not set");
		goto fail_no_dev;
	}

	dev = avb_name_to_dev_t(avb_vbmeta_device);
	if (!dev) {
		DMERR("No matching partition for device: %s",
		      avb_vbmeta_device);
		goto fail_no_dev;
	}

	invalidate_vbmeta(dev);

fail_no_dev:
	;
}

static int __init dm_verity_avb_init(void)
{
	DMINFO("AVB error handler initialized with vbmeta device: %s",
	       avb_vbmeta_device);
	return 0;
}

static void __exit dm_verity_avb_exit(void)
{
}

module_init(dm_verity_avb_init);
module_exit(dm_verity_avb_exit);

MODULE_AUTHOR("David Zeuthen <zeuthen@google.com>");
MODULE_DESCRIPTION("AVB-specific error handler for dm-verity");
MODULE_LICENSE("GPL");

/* Declare parameter with no module prefix */
#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX	"androidboot.vbmeta."
module_param_string(device, avb_vbmeta_device, sizeof(avb_vbmeta_device), 0);
module_param_string(invalidate_on_error, avb_invalidate_on_error,
		    sizeof(avb_invalidate_on_error), 0);
