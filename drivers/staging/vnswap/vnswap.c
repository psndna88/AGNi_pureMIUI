/*
 * Virtual Nand Swap Device which simulates Swap Area
 *
 * Copyright (C) 2013 SungHwan Yun
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/mempool.h>
#include <linux/pagemap.h>
#include <linux/time.h>
#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include "vnswap.h"

/* Globals */
static int vnswap_major;
struct vnswap *vnswap_device;
struct page *swap_header_page;

/*
 * Description : virtual swp_offset -> backing storage block Mapping table
 * Value : 1 ~ Max swp_entry (used) , 0 (free)
 * For example,
 * vnswap_table [1] = 0, vnswap_table [3] = 1, vnswap_table [6] = 2,
 * vnswap_table [7] = 3,
 * vnswap_table [10] = 4, vnswap_table [Others] = -1
 */
static DEFINE_SPINLOCK(vnswap_table_lock);
int *vnswap_table;

/* Backing Storage bitmap information */
unsigned long *backing_storage_bitmap;
unsigned int backing_storage_bitmap_last_allocated_index = -1;

/* Backing Storage bmap and bdev information */
sector_t *backing_storage_bmap;
struct block_device *backing_storage_bdev;
struct file *backing_storage_file;

static DEFINE_SPINLOCK(vnswap_original_bio_lock);

void vnswap_init_disksize(u64 disksize)
{
	int i;
	vnswap_device->disksize = PAGE_ALIGN(disksize);
	if ((vnswap_device->disksize/PAGE_SIZE > MAX_SWAP_AREA_SIZE_PAGES) ||
		!vnswap_device->disksize) {
		pr_err("%s %d: disksize is invalid (disksize = %llu)\n",
				__func__, __LINE__, vnswap_device->disksize);
		vnswap_device->disksize = 0;
		vnswap_device->init_success = VNSWAP_INIT_DISKSIZE_FAIL;
		return;
	}
	set_capacity(vnswap_device->disk,
		vnswap_device->disksize >> SECTOR_SHIFT);

	vnswap_table = vmalloc((vnswap_device->disksize/PAGE_SIZE) *
					sizeof(int));
	if (vnswap_table == NULL) {
		pr_err("%s %d: alloc vnswap_table is failed.\n",
				__func__, __LINE__);
		vnswap_device->init_success = VNSWAP_INIT_DISKSIZE_FAIL;
		return;
	}
	for (i = 0; i < vnswap_device->disksize/PAGE_SIZE; i++)
		vnswap_table[i] = -1;
	vnswap_device->init_success = VNSWAP_INIT_DISKSIZE_SUCCESS;
}

int vnswap_init_backing_storage(void)
{
	struct address_space *mapping;
	struct inode *inode = NULL;
	unsigned blkbits, blocks_per_page;
	sector_t probe_block, last_block, first_block;
	sector_t discard_start_block = 0, discard_last_block = 0;
	int ret = 0, i;
	mm_segment_t oldfs;
	struct timeval discard_start, discard_end;
	int discard_time;

	if (!vnswap_device ||
		vnswap_device->init_success != VNSWAP_INIT_DISKSIZE_SUCCESS) {
		ret = -EINVAL;
		pr_err("%s %d: init disksize is failed." \
				"So we can not go ahead anymore.(init_success = %d)\n",
				__func__, __LINE__,
				!vnswap_device ? -1 :
				vnswap_device->init_success);
		goto error;
	}

	oldfs = get_fs();
	set_fs(get_ds());

	backing_storage_file =
		filp_open(vnswap_device->backing_storage_filename,
					O_RDWR | O_LARGEFILE, 0);

	if (IS_ERR(backing_storage_file)) {
		ret = PTR_ERR(backing_storage_file);
		vnswap_device->stats.vnswap_backing_storage_open_fail =
			PTR_ERR(backing_storage_file);
		backing_storage_file = NULL;
		set_fs(oldfs);
		pr_err("%s %d: filp_open failed" \
				"(backing_storage_file, error, " \
				"backing_storage_filename)" \
				" = (0x%08x, 0x%08x, %s)\n",
				__func__, __LINE__,
				(unsigned int) backing_storage_file,
				ret, vnswap_device->backing_storage_filename);
		goto error;
	} else {
		set_fs(oldfs);
		vnswap_device->stats.vnswap_backing_storage_open_fail = 0;
		dprintk("%s %d: filp_open success" \
				"(backing_storage_file, error, backing_storage_filename)"
				"= (0x%08x, 0x%08x, %s)\n",
				__func__, __LINE__,
				(unsigned int) backing_storage_file,
				ret, vnswap_device->backing_storage_filename);
	}

	mapping = backing_storage_file->f_mapping;
	inode = mapping->host;
	backing_storage_bdev = inode->i_sb->s_bdev;

	if (!S_ISREG(inode->i_mode)) {
		ret = -EINVAL;
		pr_err("%s %d: backing storage file is not regular file" \
				"(inode->i_mode = %d)\n",
				__func__, __LINE__, inode->i_mode);
		goto close_file;
	}

	inode->i_flags |= S_IMMUTABLE;

	blkbits = inode->i_blkbits;
	blocks_per_page = PAGE_SIZE >> blkbits;

	if (blocks_per_page != 1) {
		ret = -EINVAL;
		pr_err("%s %d: blocks_per_page is not 1. " \
				"(blocks_per_page, inode->i_blkbits) = (%d, %d)\n",
				__func__, __LINE__,
				blocks_per_page, inode->i_blkbits);
		goto close_file;
	}

	last_block = i_size_read(inode) >> blkbits;
	vnswap_device->bs_size = last_block;
	vnswap_device->stats.vnswap_total_slot_num = last_block;

	if ((vnswap_device->bs_size > MAX_BACKING_STORAGE_SIZE_PAGES) ||
		!vnswap_device->bs_size) {
		ret = -EINVAL;
		pr_err("%s %d: backing storage size is invalid." \
				"(backing storage size = %llu)\n",
				__func__, __LINE__,
				vnswap_device->bs_size);
		goto close_file;
	}

	/*
	* Align sizeof(unsigned long) * 8 page
	*  - This alignment is for Integer (sizeof(unsigned long) Bytes ->
	*    sizeof(unsigned long) * 8 Bit -> sizeof(unsigned long) * 8 page)
	*    bitmap operation
	*/
	if (vnswap_device->bs_size % (sizeof(unsigned long)*8) != 0) {
		dprintk("%s %d: backing storage size is misaligned " \
				"(32 page align)." \
				"So, it is truncated from %llu pages to %llu pages\n",
				__func__, __LINE__, vnswap_device->bs_size,
				vnswap_device->bs_size /
				(sizeof(unsigned long)*8)*
				(sizeof(unsigned long)*8));
		vnswap_device->bs_size = (vnswap_device->bs_size /
			(sizeof(unsigned long)*8) * (sizeof(unsigned long)*8));
	}

	backing_storage_bitmap = vmalloc(vnswap_device->bs_size / 8);
	if (backing_storage_bitmap == NULL) {
		ret = -ENOMEM;
		goto close_file;
	}

	for (i = 0; i < vnswap_device->bs_size / 32; i++)
		backing_storage_bitmap[i] = 0;
	backing_storage_bitmap_last_allocated_index = -1;

	backing_storage_bmap = vmalloc(vnswap_device->bs_size *
							sizeof(sector_t));
	if (backing_storage_bmap == NULL) {
		ret = -ENOMEM;
		goto free_bitmap;
	}

	for (probe_block = 0; probe_block < last_block; probe_block++) {
		first_block = bmap(inode, probe_block);
		if (first_block == 0) {
			pr_err("%s %d: backing_storage file has holes." \
					"(probe_block, first_block) = (%llu,%llu)\n",
					__func__, __LINE__,
					probe_block, first_block);
			ret = -EINVAL;
			goto free_bmap;
		}
		backing_storage_bmap[probe_block] = first_block;

		/* new extent */
		if (discard_start_block == 0) {
			discard_start_block = discard_last_block = first_block;
			continue;
		}

		/* first block is a member of extent */
		if (discard_last_block+1 == first_block) {
			discard_last_block++;
			continue;
		}

		/*
		* first block is not a member of extent
		* discard current extent.
		*/
		do_gettimeofday(&discard_start);
		ret = blkdev_issue_discard(backing_storage_bdev,
				discard_start_block << (PAGE_SHIFT - 9),
				(discard_last_block - discard_start_block + 1)
				<< (PAGE_SHIFT - 9), GFP_KERNEL, 0);
		do_gettimeofday(&discard_end);

		discard_time = (discard_end.tv_sec - discard_start.tv_sec) *
			USEC_PER_SEC +
			(discard_end.tv_usec - discard_start.tv_usec);

		if (ret) {
			pr_err("%s %d: blkdev_issue_discard failed. (ret) = (%d)\n",
					__func__, __LINE__, ret);
			goto free_bmap;
		}
		dprintk("%s %d: blkdev_issue_discard success" \
				"(start, size, discard_time) = (%llu, %llu, %d)\n",
				__func__, __LINE__, discard_start_block,
				discard_last_block - discard_start_block + 1,
				discard_time);
		discard_start_block = discard_last_block = first_block;
	}

	/* last extent */
	if (discard_start_block) {
		do_gettimeofday(&discard_start);
		ret = blkdev_issue_discard(backing_storage_bdev,
				discard_start_block << (PAGE_SHIFT - 9),
				(discard_last_block - discard_start_block + 1)
				<< (PAGE_SHIFT - 9), GFP_KERNEL, 0);
		do_gettimeofday(&discard_end);
		discard_time = (discard_end.tv_sec - discard_start.tv_sec) *
			USEC_PER_SEC +
			(discard_end.tv_usec - discard_start.tv_usec);
		if (ret) {
			pr_err("%s %d: blkdev_issue_discard failed. (ret) = (%d)\n",
					__func__, __LINE__, ret);
			goto free_bmap;
		}
		dprintk("%s %d: blkdev_issue_discard success" \
				"(start, size, discard_time) = (%llu, %llu, %d)\n",
				__func__, __LINE__, discard_start_block,
				discard_last_block - discard_start_block + 1,
				discard_time);
		discard_start_block = discard_last_block = 0;
	}

	vnswap_device->init_success |= VNSWAP_INIT_BACKING_STORAGE_SUCCESS;
	return ret;

free_bmap:
	vfree(backing_storage_bmap);

free_bitmap:
	vfree(backing_storage_bitmap);

close_file:
	filp_close(backing_storage_file, NULL);

error:
	if (vnswap_device)
		vnswap_device->init_success |= VNSWAP_INIT_BACKING_STORAGE_FAIL;
	return ret;
}

/* find free area (nand_offset, page_offset) in backing storage */
int vnswap_find_free_area_in_backing_storage(int *nand_offset)
{
	int i, found = 0;

	/* Backing Storage is full */
	if (backing_storage_bitmap_last_allocated_index ==
		vnswap_device->bs_size) {
		atomic_inc(&vnswap_device->stats.
			vnswap_backing_storage_full_num);
		return -ENOSPC;
	}

	for (i = backing_storage_bitmap_last_allocated_index + 1;
		i < vnswap_device->bs_size; i++)
		if (!test_bit(i, backing_storage_bitmap)) {
			found = 1;
			break;
		}

	if (!found) {
		for (i = 0;
			i < backing_storage_bitmap_last_allocated_index;
			i++)
			if (!test_bit(i, backing_storage_bitmap)) {
				found = 1;
				break;
			}
	}

	/* Backing Storage is full */
	if (!found) {
		backing_storage_bitmap_last_allocated_index =
			vnswap_device->bs_size;
		atomic_inc(&vnswap_device->stats.
			vnswap_backing_storage_full_num);
		return -ENOSPC;
	}
	*nand_offset =
		backing_storage_bitmap_last_allocated_index = i;
	return i;
}

/* refer req_bio_endio() */
void vnswap_bio_end_read(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio *original_bio = (struct bio *) bio->bi_private;
	unsigned long flags;

	dprintk("%s %d: (uptodate,error,bi_size) = (%d, %d, %d)\n",
			__func__, __LINE__, uptodate, err, bio->bi_size);

	if (!uptodate || err) {
		atomic_inc(&vnswap_device->stats.vnswap_bio_end_fail_r1_num);
		pr_err("%s %d: (error, bio->bi_size, original_bio->bi_size," \
				"bio->bi_vcnt, original_bio->bi_vcnt, " \
				"bio->bi_idx," \
				"original_bio->bi_idx, " \
				"vnswap_bio_end_fail_r1_num ~" \
				"vnswap_bio_end_fail_r3_num) =" \
				"(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
				__func__, __LINE__, err, bio->bi_size,
				original_bio->bi_size,
				bio->bi_vcnt, original_bio->bi_vcnt,
				bio->bi_idx,
				original_bio->bi_idx,
				vnswap_device->stats.
					vnswap_bio_end_fail_r1_num.counter,
				vnswap_device->stats.
					vnswap_bio_end_fail_r2_num.counter,
				vnswap_device->stats.
					vnswap_bio_end_fail_r3_num.counter);
		bio_io_error(original_bio);
		goto out_bio_put;
	} else {
		/*
		* There are bytes yet to be transferred.
		* blk_end_request() -> blk_end_bidi_request() ->
		* blk_update_bidi_request() ->
		* blk_update_request() -> req_bio_endio() ->
		* bio->bi_size -= nbytes;
		*/
		spin_lock_irqsave(&vnswap_original_bio_lock, flags);
		original_bio->bi_size -= (PAGE_SIZE-bio->bi_size);

		if (bio->bi_size == PAGE_SIZE) {
			atomic_inc(&vnswap_device->stats.
				vnswap_bio_end_fail_r2_num);
			pr_err("%s %d: (error, bio->bi_size, " \
					"original_bio->bi_size," \
					"bio->bi_vcnt," \
					"original_bio->bi_vcnt, bio->bi_idx, " \
					"original_bio->bi_idx," \
					"vnswap_bio_end_fail_r1_num ~ " \
					"vnswap_bio_end_fail_r3_num) = " \
					"(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
					__func__, __LINE__, err, bio->bi_size,
					original_bio->bi_size,
					bio->bi_vcnt, original_bio->bi_vcnt,
					bio->bi_idx,
					original_bio->bi_idx,
					vnswap_device->stats.
						vnswap_bio_end_fail_r1_num.
						counter,
					vnswap_device->stats.
						vnswap_bio_end_fail_r2_num.
						counter,
					vnswap_device->stats.
						vnswap_bio_end_fail_r3_num.
						counter);
			spin_unlock_irqrestore(&vnswap_original_bio_lock,
				flags);
			goto out_bio_put;
		}

		if (bio->bi_size || original_bio->bi_size) {
			atomic_inc(&vnswap_device->stats.
				vnswap_bio_end_fail_r3_num);
			pr_err("%s %d: (error, bio->bi_size, " \
					"original_bio->bi_size," \
					"bio->bi_vcnt," \
					"original_bio->bi_vcnt, bio->bi_idx, " \
					"original_bio->bi_idx," \
					"vnswap_bio_end_fail_r1_num ~ " \
					"vnswap_bio_end_fail_r3_num) = " \
					"(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
					__func__, __LINE__, err, bio->bi_size,
					original_bio->bi_size,
					bio->bi_vcnt, original_bio->bi_vcnt,
					bio->bi_idx, original_bio->bi_idx,
					vnswap_device->stats.
						vnswap_bio_end_fail_r1_num.
						counter,
					vnswap_device->stats.
						vnswap_bio_end_fail_r2_num.
						counter,
					vnswap_device->stats.
						vnswap_bio_end_fail_r3_num.
						counter);
			spin_unlock_irqrestore(&vnswap_original_bio_lock,
				flags);
			goto out_bio_put;
		}

		set_bit(BIO_UPTODATE, &original_bio->bi_flags);
		spin_unlock_irqrestore(&vnswap_original_bio_lock, flags);
		bio_endio(original_bio, 0);
	}

out_bio_put:
	bio_put(bio);
}

/* refer req_bio_endio() */
void vnswap_bio_end_write(struct bio *bio, int err)
{
	struct bio *original_bio = (struct bio *) bio->bi_private;
	unsigned long flags;

	dprintk("%s %d: (error, bi_size) = (%d, %d)\n",
			__func__, __LINE__, err, bio->bi_size);

	if (err) {
		atomic_inc(&vnswap_device->stats.vnswap_bio_end_fail_w1_num);
		pr_err("%s %d: (error, bio->bi_size, original_bio->bi_size, " \
				"bio->bi_vcnt," \
				"original_bio->bi_vcnt, bio->bi_idx, " \
				"original_bio->bi_idx," \
				"vnswap_bio_end_fail_w1_num ~ " \
				"vnswap_bio_end_fail_w3_num) = " \
				"(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
				__func__, __LINE__, err, bio->bi_size,
				original_bio->bi_size,
				bio->bi_vcnt, original_bio->bi_vcnt,
				bio->bi_idx,
				original_bio->bi_idx,
				vnswap_device->stats.
					vnswap_bio_end_fail_w1_num.counter,
				vnswap_device->stats.
					vnswap_bio_end_fail_w2_num.counter,
				vnswap_device->stats.
					vnswap_bio_end_fail_w3_num.counter);
		bio_io_error(original_bio);
		goto out_bio_put;
	} else {
		/*
		* There are bytes yet to be transferred.
		* blk_end_request() -> blk_end_bidi_request() ->
		* blk_update_bidi_request() ->
		* blk_update_request() -> req_bio_endio() ->
		* bio->bi_size -= nbytes;
		*/
		spin_lock_irqsave(&vnswap_original_bio_lock, flags);
		original_bio->bi_size -= (PAGE_SIZE-bio->bi_size);

		if (bio->bi_size == PAGE_SIZE) {
			atomic_inc(&vnswap_device->stats.
				vnswap_bio_end_fail_w2_num);
			pr_err("%s %d: (error, bio->bi_size, " \
					"original_bio->bi_size, " \
					"bio->bi_vcnt," \
					"original_bio->bi_vcnt, bio->bi_idx, " \
					"original_bio->bi_idx," \
					"vnswap_bio_end_fail_w1_num ~ " \
					"vnswap_bio_end_fail_w3_num) = " \
					"(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
					__func__, __LINE__, err, bio->bi_size,
					original_bio->bi_size,
					bio->bi_vcnt, original_bio->bi_vcnt,
					bio->bi_idx,
					original_bio->bi_idx,
					vnswap_device->stats.
						vnswap_bio_end_fail_w1_num.
						counter,
					vnswap_device->stats.
						vnswap_bio_end_fail_w2_num.
						counter,
					vnswap_device->stats.
						vnswap_bio_end_fail_w3_num.
						counter);
			spin_unlock_irqrestore(&vnswap_original_bio_lock,
				flags);
			goto out_bio_put;
		}

		if (bio->bi_size || original_bio->bi_size) {
			atomic_inc(&vnswap_device->stats.
				vnswap_bio_end_fail_w3_num);
			pr_err("%s %d: (error, bio->bi_size, " \
					"original_bio->bi_size, " \
					"bio->bi_vcnt," \
					"original_bio->bi_vcnt, bio->bi_idx, " \
					"original_bio->bi_idx," \
					"vnswap_bio_end_fail_w1_num ~ " \
					"vnswap_bio_end_fail_w3_num) = " \
					"(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
					__func__, __LINE__, err, bio->bi_size,
					original_bio->bi_size,
					bio->bi_vcnt, original_bio->bi_vcnt,
					bio->bi_idx,
					original_bio->bi_idx,
					vnswap_device->stats.
						vnswap_bio_end_fail_w1_num.
						counter,
					vnswap_device->stats.
						vnswap_bio_end_fail_w2_num.
						counter,
					vnswap_device->stats.
						vnswap_bio_end_fail_w3_num.
						counter);
			spin_unlock_irqrestore(&vnswap_original_bio_lock,
				flags);
			goto out_bio_put;
		}

		set_bit(BIO_UPTODATE, &original_bio->bi_flags);
		spin_unlock_irqrestore(&vnswap_original_bio_lock,
			flags);
		bio_endio(original_bio, 0);
	}

out_bio_put:
	bio_put(bio);
}

/* Insert entry into VNSWAP_IO sub system */
int vnswap_submit_bio(int rw, int nand_offset,
	struct page *page, struct bio *original_bio)
{
	struct bio *bio;
	int ret = 0;

	if (!rw) {
		VM_BUG_ON(!PageLocked(page));
		VM_BUG_ON(PageUptodate(page));
	}

	bio = bio_alloc(GFP_NOIO, 1);

	if (!bio) {
		atomic_inc(&vnswap_device->stats.vnswap_bio_no_mem_num);
		ret = -ENOMEM;
		goto out;
	}

	bio->bi_sector = (backing_storage_bmap[nand_offset] <<
					(PAGE_SHIFT - 9));
	bio->bi_bdev = backing_storage_bdev;
	bio->bi_io_vec[0].bv_page = page;
	bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	bio->bi_io_vec[0].bv_offset = 0;
	bio->bi_vcnt = 1;
	bio->bi_idx = 0;
	bio->bi_size = PAGE_SIZE;
	bio->bi_private = (void *) original_bio;
	if (rw)
		bio->bi_end_io = vnswap_bio_end_write;
	else
		bio->bi_end_io = vnswap_bio_end_read;

	dprintk("%s %d: (rw, nand_offset) = (%d,%d)\n",
			__func__, __LINE__, rw, nand_offset);

	submit_bio(rw, bio);

	if (rw) {
		atomic_inc(&vnswap_device->stats.
			vnswap_stored_pages);
		atomic_inc(&vnswap_device->stats.
			vnswap_write_pages);
	} else {
		atomic_inc(&vnswap_device->stats.
			vnswap_read_pages);
	}
	/* TODO: check bio->bi_flags */

out:
	return ret;
}

int vnswap_bvec_read(struct vnswap *vnswap, struct bio_vec *bvec,
	u32 index, struct bio *bio)
{
	struct page *page;
	unsigned char *user_mem, *swap_header_page_mem;
	int nand_offset = 0, ret = 0;

	page = bvec->bv_page;

	/* swap header */
	if (index == 0) {
		user_mem = kmap_atomic(page);
		swap_header_page_mem = kmap_atomic(swap_header_page);
		memcpy(user_mem, swap_header_page_mem, bvec->bv_len);
		kunmap_atomic(swap_header_page_mem);
		kunmap_atomic(user_mem);
		flush_dcache_page(page);
		return 0;
	}

	spin_lock(&vnswap_table_lock);
	nand_offset = vnswap_table[index];
	if (nand_offset == -1) {
		pr_err("%s %d: vnswap_table is not mapped. " \
				"(index, nand_offset)" \
				"= (%d, %d)\n", __func__, __LINE__,
				index, nand_offset);
		ret = -EIO;
		atomic_inc(&vnswap_device->stats.
			vnswap_not_mapped_read_pages);
		spin_unlock(&vnswap_table_lock);
		goto out;
	}
	spin_unlock(&vnswap_table_lock);

	dprintk("%s %d: (index, nand_offset) = (%d, %d)\n",
			__func__, __LINE__, index, nand_offset);

	/* Read nand_offset position backing storage into page */
	ret = vnswap_submit_bio(0, nand_offset, page, bio);

out:
	return ret;
}

int vnswap_bvec_write(struct vnswap *vnswap, struct bio_vec *bvec,
	u32 index, struct bio *bio)
{
	struct page *page;
	unsigned char *user_mem, *swap_header_page_mem;
	int nand_offset = 0, ret;

	page = bvec->bv_page;

	/* swap header */
	if (index == 0) {
		user_mem = kmap_atomic(page);
		swap_header_page_mem = kmap_atomic(swap_header_page);
		memcpy(swap_header_page_mem, user_mem, PAGE_SIZE);
		kunmap_atomic(swap_header_page_mem);
		kunmap_atomic(user_mem);
		return 0;
	}

	spin_lock(&vnswap_table_lock);
	nand_offset = vnswap_table[index];

	/* duplicate write - remove existing mapping */
	if (nand_offset != -1) {
		atomic_inc(&vnswap_device->stats.
			vnswap_double_mapped_slot_num);
		clear_bit(nand_offset, backing_storage_bitmap);
		vnswap_table[index] = -1;
		atomic_dec(&vnswap_device->stats.
			vnswap_used_slot_num);
		atomic_dec(&vnswap_device->stats.
			vnswap_stored_pages);
	}

	ret = vnswap_find_free_area_in_backing_storage(&nand_offset);
	if (ret < 0) {
		spin_unlock(&vnswap_table_lock);
		return ret;
	}
	set_bit(nand_offset, backing_storage_bitmap);
	vnswap_table[index] = nand_offset;
	atomic_inc(&vnswap_device->stats.
		vnswap_used_slot_num);
	spin_unlock(&vnswap_table_lock);

	dprintk("%s %d: (index, nand_offset) = (%d, %d)\n",
			__func__, __LINE__, index, nand_offset);
	ret = vnswap_submit_bio(1, nand_offset, page, bio);

	if (ret) {
		spin_lock(&vnswap_table_lock);
		clear_bit(nand_offset, backing_storage_bitmap);
		vnswap_table[index] = -1;
		spin_unlock(&vnswap_table_lock);
	}

	return ret;
}

int vnswap_bvec_rw(struct vnswap *vnswap, struct bio_vec *bvec,
	u32 index, struct bio *bio, int rw)
{
	int ret;

	if (rw == READ) {
		down_read(&vnswap->lock);
		dprintk("%s %d: (rw,index) = (%d, %d)\n",
			__func__, __LINE__, rw, index);
		ret = vnswap_bvec_read(vnswap, bvec, index, bio);
		up_read(&vnswap->lock);
	} else {
		down_write(&vnswap->lock);
		dprintk("%s %d: (rw,index) = (%d, %d)\n",
			__func__, __LINE__, rw, index);
		ret = vnswap_bvec_write(vnswap, bvec, index, bio);
		up_write(&vnswap->lock);
	}

	return ret;
}

void __vnswap_make_request(struct vnswap *vnswap,
	struct bio *bio, int rw)
{
	int i, offset, ret;
	u32 index, is_swap_header_page;
	struct bio_vec *bvec;

	index = bio->bi_sector >> SECTORS_PER_PAGE_SHIFT;
	offset = (bio->bi_sector & (SECTORS_PER_PAGE - 1)) <<
				SECTOR_SHIFT;

	if (index == 0)
		is_swap_header_page = 1;
	else
		is_swap_header_page = 0;

	dprintk("%s %d: (rw, index, offset, bi_size) = " \
			"(%d, %d, %d, %d)\n",
			__func__, __LINE__,
			rw, index, offset, bio->bi_size);

	if (offset) {
		atomic_inc(&vnswap_device->stats.
			vnswap_bio_invalid_num);
		pr_err("%s %d: invalid offset. " \
				"(bio->bi_sector, index, offset," \
				"vnswap_bio_invalid_num) = (%llu, %d, %d, %d)\n",
				__func__, __LINE__, bio->bi_sector,
				index, offset,
				vnswap_device->stats.
					vnswap_bio_invalid_num.counter);
		goto out_error;
	}

	if (bio->bi_size > PAGE_SIZE) {
		atomic_inc(&vnswap_device->stats.
			vnswap_bio_large_bi_size_num);
		goto out_error;
	}

	if (bio->bi_vcnt > 1) {
		atomic_inc(&vnswap_device->stats.
			vnswap_bio_large_bi_vcnt_num);
		goto out_error;
	}

	bio_for_each_segment(bvec, bio, i) {
		if (bvec->bv_len != PAGE_SIZE || bvec->bv_offset != 0) {
			atomic_inc(&vnswap_device->stats.
				vnswap_bio_invalid_num);
			pr_err("%s %d: bvec is misaligned. " \
					"(bv_len, bv_offset," \
					"vnswap_bio_invalid_num) = (%d, %d, %d)\n",
					__func__, __LINE__,
					bvec->bv_len, bvec->bv_offset,
					vnswap_device->stats.
						vnswap_bio_invalid_num.counter);
			goto out_error;
		}

		dprintk("%s %d: (rw, index, bvec->bv_len) = " \
				"(%d, %d, %d)\n",
				__func__, __LINE__, rw, index, bvec->bv_len);

		ret = vnswap_bvec_rw(vnswap, bvec, index, bio, rw);
		if (ret < 0) {
			if (ret != -ENOSPC)
				pr_err("%s %d: vnswap_bvec_rw failed." \
						"(ret) = (%d)\n",
						__func__, __LINE__, ret);
			else
				dprintk("%s %d: vnswap_bvec_rw failed. " \
				"(ret) = (%d)\n",
						__func__, __LINE__, ret);
			goto out_error;
		}

		index++;
	}

	if (is_swap_header_page) {
		set_bit(BIO_UPTODATE, &bio->bi_flags);
		bio_endio(bio, 0);
	}

	return;

out_error:
	bio_io_error(bio);
}

/*
 * Check if request is within bounds and aligned on vnswap logical blocks.
 */
static inline int vnswap_valid_io_request(struct vnswap *vnswap,
	struct bio *bio)
{
	if (unlikely(
		(bio->bi_sector >= (vnswap->disksize >> SECTOR_SHIFT)) ||
		(bio->bi_sector & (VNSWAP_SECTOR_PER_LOGICAL_BLOCK - 1)) ||
		(bio->bi_size & (VNSWAP_LOGICAL_BLOCK_SIZE - 1)))) {

		return 0;
	}

	/* I/O request is valid */
	return 1;
}

/*
 * Handler function for all vnswap I/O requests.
 */
void vnswap_make_request(struct request_queue *queue, struct bio *bio)
{
	struct vnswap *vnswap = queue->queuedata;

	/* disable NAND I/O when DiskSize is not initialized */
	if (!(vnswap_device->init_success & VNSWAP_INIT_DISKSIZE_SUCCESS))
		goto error;

	/*
	 * disable NAND I/O when Backing Storage is not initialized and
	 * is not swap_header_page
	 */
	if (!(vnswap_device->init_success &
		VNSWAP_INIT_BACKING_STORAGE_SUCCESS)
		&& (bio->bi_sector >> SECTORS_PER_PAGE_SHIFT))
		goto error;

	if (!vnswap_valid_io_request(vnswap, bio)) {
		atomic_inc(&vnswap_device->stats.
			vnswap_bio_invalid_num);
		pr_err("%s %d: invalid io request. " \
				"(bio->bi_sector, bio->bi_size," \
				"vnswap->disksize, vnswap_bio_invalid_num) = " \
				"(%llu, %d, %llu, %d)\n",
				__func__, __LINE__,
				bio->bi_sector, bio->bi_size,
				vnswap->disksize,
				vnswap_device->stats.
					vnswap_bio_invalid_num.counter);
		goto error;
	}

	__vnswap_make_request(vnswap, bio, bio_data_dir(bio));
	return;

error:
	bio_io_error(bio);
}

void vnswap_slot_free_notify(struct block_device *bdev, unsigned long index)
{
	struct vnswap *vnswap;
	int nand_offset = 0;

	vnswap = bdev->bd_disk->private_data;

	spin_lock(&vnswap_table_lock);
	nand_offset = vnswap_table[index];

	/* This index is not mapped to vnswap and is mapped to zswap */
	if (nand_offset == -1) {
		atomic_inc(&vnswap_device->stats.
			vnswap_not_mapped_slot_free_num);
		spin_unlock(&vnswap_table_lock);
		return;
	}

	atomic_inc(&vnswap_device->stats.
		vnswap_mapped_slot_free_num);
	atomic_dec(&vnswap_device->stats.
		vnswap_stored_pages);
	atomic_dec(&vnswap_device->stats.
		vnswap_used_slot_num);
	clear_bit(nand_offset, backing_storage_bitmap);
	vnswap_table[index] = -1;

	/* When Backing Storage is full, set Backing Storage is not full */
	if (backing_storage_bitmap_last_allocated_index ==
		vnswap_device->bs_size) {
		backing_storage_bitmap_last_allocated_index = nand_offset;
	}
	spin_unlock(&vnswap_table_lock);

	/*
	 * disable blkdev_issue_discard
	 * - BUG: scheduling while atomic: rild/4248/0x00000003
	*   blkdev_issue_discard() -> wait_for_completion() ->
	 *	wait_for_common() -> schedule_timeout() -> schedule()
	 */
#if 0
	/* discard nand_offset position Backing Storage for security */
	ret = blkdev_issue_discard(backing_storage_bdev,
			(backing_storage_bmap[nand_offset] << (PAGE_SHIFT - 9)),
			1 << (PAGE_SHIFT - 9), GFP_KERNEL, 0);
	if (ret)
		dprintk("vnswap_slot_free_notify: " \
		"blkdev_issue_discard is failed\n");
#endif
}

const struct block_device_operations vnswap_devops = {
	.swap_slot_free_notify = vnswap_slot_free_notify,
	.owner = THIS_MODULE
};

static int create_device(struct vnswap *vnswap)
{
	int ret = 0;

	init_rwsem(&vnswap->lock);

	vnswap->queue = blk_alloc_queue(GFP_KERNEL);
	if (!vnswap->queue) {
		pr_err("%s %d: Error allocating disk queue for device\n",
				__func__, __LINE__);
		ret = -ENOMEM;
		goto out;
	}

	blk_queue_make_request(vnswap->queue, vnswap_make_request);
	vnswap->queue->queuedata = vnswap;

	 /* gendisk structure */
	vnswap->disk = alloc_disk(1);
	if (!vnswap->disk) {
		blk_cleanup_queue(vnswap->queue);
		pr_err("%s %d: Error allocating disk structure for device\n",
				__func__, __LINE__);
		ret = -ENOMEM;
		goto out_free_queue;
	}

	vnswap->disk->major = vnswap_major;
	vnswap->disk->first_minor = 0;
	vnswap->disk->fops = &vnswap_devops;
	vnswap->disk->queue = vnswap->queue;
	vnswap->disk->private_data = vnswap;
	snprintf(vnswap->disk->disk_name, 16, "vnswap%d", 0);

	/* Actual capacity set using sysfs (/sys/block/vnswap<id>/disksize) */
	set_capacity(vnswap->disk, 0);

	/*
	 * To ensure that we always get PAGE_SIZE aligned
	 * and n*PAGE_SIZED sized I/O requests.
	 */
	blk_queue_physical_block_size(vnswap->disk->queue,
		PAGE_SIZE);
	blk_queue_logical_block_size(vnswap->disk->queue,
		VNSWAP_LOGICAL_BLOCK_SIZE);
	blk_queue_io_min(vnswap->disk->queue, PAGE_SIZE);
	blk_queue_io_opt(vnswap->disk->queue, PAGE_SIZE);
	blk_queue_max_hw_sectors(vnswap->disk->queue,
		PAGE_SIZE / SECTOR_SIZE);

	add_disk(vnswap->disk);

	vnswap->disksize = 0;
	vnswap->bs_size = 0;
	vnswap->init_success = 0;

	ret = sysfs_create_group(&disk_to_dev(vnswap->disk)->kobj,
			&vnswap_disk_attr_group);
	if (ret < 0) {
		pr_err("%s %d: Error creating sysfs group\n",
			__func__, __LINE__);
		goto out_put_disk;
	}

	/* vnswap devices sort of resembles non-rotational disks */
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT,
		vnswap->disk->queue);

	swap_header_page =  alloc_page(__GFP_HIGHMEM);

	if (!swap_header_page) {
		pr_err("%s %d: Error creating swap_header_page\n",
			__func__, __LINE__);
		ret = -ENOMEM;
		goto remove_vnswap_group;
	}

out:
	return ret;

remove_vnswap_group:
	sysfs_remove_group(&disk_to_dev(vnswap->disk)->kobj,
		&vnswap_disk_attr_group);

out_put_disk:
	put_disk(vnswap->disk);

out_free_queue:
	blk_cleanup_queue(vnswap->queue);

	return ret;
}

void destroy_device(struct vnswap *vnswap)
{
	if (vnswap->disk)
		sysfs_remove_group(&disk_to_dev(vnswap->disk)->kobj,
			&vnswap_disk_attr_group);

	if (vnswap->disk) {
		del_gendisk(vnswap->disk);
		put_disk(vnswap->disk);
	}

	if (vnswap->queue)
		blk_cleanup_queue(vnswap->queue);
}

int __init vnswap_init(void)
{
	int ret = 0;

	vnswap_major = register_blkdev(0, "vnswap");
	if (vnswap_major <= 0) {
		pr_err("%s %d: Unable to get major number\n",
			__func__, __LINE__);
		ret = -EBUSY;
		goto out;
	}

	/* Initialize global variables */
	vnswap_table = NULL;
	backing_storage_bitmap = NULL;
	backing_storage_bmap = NULL;
	backing_storage_bdev = NULL;
	backing_storage_file = NULL;

	/* Allocate and initialize the device */
	vnswap_device = kzalloc(sizeof(struct vnswap), GFP_KERNEL);
	if (!vnswap_device) {
		ret = -ENOMEM;
		pr_err("%s %d: Unable to allocate vnswap_device\n",
			__func__, __LINE__);
		goto unregister;
	}

	ret = create_device(vnswap_device);
	if (ret) {
		pr_err("%s %d: Unable to create vnswap_device\n",
			__func__, __LINE__);
		goto free_devices;
	}

	vnswap_device->stats.vnswap_is_init = 1;

	return 0;

free_devices:
	kfree(vnswap_device);

unregister:
	unregister_blkdev(vnswap_major, "vnswap");

out:
	return ret;
}

void __exit vnswap_exit(void)
{
	destroy_device(vnswap_device);

	unregister_blkdev(vnswap_major, "vnswap");

	if (backing_storage_file)
		filp_close(backing_storage_file, NULL);
	if (swap_header_page)
		__free_page(swap_header_page);
	kfree(vnswap_device);
	if (backing_storage_bmap)
		vfree(backing_storage_bmap);
	if (backing_storage_bitmap)
		vfree(backing_storage_bitmap);
	if (vnswap_table)
		vfree(vnswap_table);

	dprintk("%s %d: Cleanup done!\n", __func__, __LINE__);
}

module_init(vnswap_init);
module_exit(vnswap_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("SungHwan Yun <sunghwan.yun@samsung.com>");
MODULE_DESCRIPTION("Virtual Nand Swap Device which simulates Swap Area");
