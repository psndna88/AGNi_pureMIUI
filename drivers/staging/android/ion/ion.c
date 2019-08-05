// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 Google, Inc.
 * Copyright (c) 2011-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#include <linux/dma-buf.h>
#include <linux/memblock.h>
#include <linux/miscdevice.h>
#include <linux/msm_dma_iommu_mapping.h>
#include <linux/msm_ion.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "ion.h"
#include "ion_secure_util.h"

static struct ion_device *internal_dev;
static struct kmem_cache *ion_sg_table_pool;

int ion_walk_heaps(int heap_id, enum ion_heap_type type, void *data,
		   int (*f)(struct ion_heap *heap, void *data))
{
	struct ion_device *dev = internal_dev;
	struct ion_heap *heap;
	int ret = 0;

	down_write(&dev->heap_lock);
	plist_for_each_entry(heap, &dev->heaps, node) {
		if (heap->type == type && ION_HEAP(heap->id) == heap_id) {
			ret = f(heap, data);
			break;
		}
	}
	up_write(&dev->heap_lock);

	return ret;
}

static struct ion_buffer *ion_buffer_create(struct ion_heap *heap,
					    struct ion_device *dev,
					    unsigned long len,
					    unsigned long flags)
{
	struct ion_buffer *buffer;
	struct sg_table *table;
	int ret;

	buffer = kmalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	*buffer = (typeof(*buffer)){
		.dev = dev,
		.heap = heap,
		.flags = flags,
		.size = len,
		.attachments = LIST_HEAD_INIT(buffer->attachments),
		.vmas = LIST_HEAD_INIT(buffer->vmas),
		.attachment_lock = __MUTEX_INITIALIZER(buffer->attachment_lock),
		.kmap_lock = __MUTEX_INITIALIZER(buffer->kmap_lock),
		.vma_lock = __MUTEX_INITIALIZER(buffer->vma_lock)
	};

	ret = heap->ops->allocate(heap, buffer, len, flags);
	if (ret) {
		if (!(heap->flags & ION_HEAP_FLAG_DEFER_FREE))
			goto free_buffer;

		if (ret == -EINTR)
			goto free_buffer;

		ion_heap_freelist_drain(heap, 0);
		ret = heap->ops->allocate(heap, buffer, len, flags);
		if (ret)
			goto free_buffer;
	}

	if (buffer->sg_table == NULL)
		goto free_heap;

	table = buffer->sg_table;
	return buffer;

free_heap:
	heap->ops->free(buffer);
free_buffer:
	kfree(buffer);
	return ERR_PTR(-EINVAL);
}

void ion_buffer_destroy(struct ion_buffer *buffer)
{
	buffer->heap->ops->free(buffer);
	kfree(buffer);
}

static void _ion_buffer_destroy(struct ion_buffer *buffer)
{
	struct ion_heap *heap = buffer->heap;

	msm_dma_buf_freed(buffer);

	if (heap->flags & ION_HEAP_FLAG_DEFER_FREE)
		ion_heap_freelist_add(heap, buffer);
	else
		ion_buffer_destroy(buffer);
}

static void *ion_buffer_kmap_get(struct ion_buffer *buffer)
{
	void *vaddr;

	mutex_lock(&buffer->kmap_lock);
	if (buffer->kmap_cnt) {
		vaddr = buffer->vaddr;
		buffer->kmap_cnt++;
	} else {
		vaddr = buffer->heap->ops->map_kernel(buffer->heap, buffer);
		if (IS_ERR_OR_NULL(vaddr)) {
			vaddr = ERR_PTR(-EINVAL);
		} else {
			buffer->vaddr = vaddr;
			buffer->kmap_cnt++;
		}
	}
	mutex_unlock(&buffer->kmap_lock);

	return vaddr;
}

static void ion_buffer_kmap_put(struct ion_buffer *buffer)
{
	mutex_lock(&buffer->kmap_lock);
	if (!--buffer->kmap_cnt)
		buffer->heap->ops->unmap_kernel(buffer->heap, buffer);
	mutex_unlock(&buffer->kmap_lock);
}

static struct sg_table *dup_sg_table(struct sg_table *table)
{
	struct scatterlist *sg, *new_sg;
	struct sg_table *new_table;
	int ret, i;

	new_table = kmem_cache_alloc(ion_sg_table_pool, GFP_KERNEL);
	if (!new_table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(new_table, table->nents, GFP_KERNEL);
	if (ret) {
		kmem_cache_free(ion_sg_table_pool, new_table);
		return ERR_PTR(-ENOMEM);
	}

	new_sg = new_table->sgl;
	if (new_table->nents <= SG_MAX_SINGLE_ALLOC) {
		memcpy(new_sg, table->sgl, new_table->nents * sizeof(*new_sg));
		sg_dma_address(new_sg) = 0;
		sg_dma_len(new_sg) = 0;
	} else {
		for_each_sg(table->sgl, sg, table->nents, i) {
			*new_sg = *sg;
			sg_dma_address(new_sg) = 0;
			sg_dma_len(new_sg) = 0;
			new_sg = sg_next(new_sg);
		}
	}

	return new_table;
}

static void free_duped_table(struct sg_table *table)
{
	sg_free_table(table);
	kmem_cache_free(ion_sg_table_pool, table);
}

struct ion_dma_buf_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
	bool dma_mapped;
};

static int ion_dma_buf_attach(struct dma_buf *dmabuf, struct device *dev,
				struct dma_buf_attachment *attachment)
{
	struct ion_dma_buf_attachment *a;
	struct sg_table *table;
	struct ion_buffer *buffer = dmabuf->priv;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	table = dup_sg_table(buffer->sg_table);
	if (IS_ERR(table)) {
		kfree(a);
		return -ENOMEM;
	}

	*a = (typeof(*a)){
		.table = table,
		.dev = dev
	};

	attachment->priv = a;

	if (buffer->flags & ION_FLAG_CACHED) {
		mutex_lock(&buffer->attachment_lock);
		list_add(&a->list, &buffer->attachments);
		mutex_unlock(&buffer->attachment_lock);
	}

	return 0;
}

static void ion_dma_buf_detach(struct dma_buf *dmabuf,
				struct dma_buf_attachment *attachment)
{
	struct ion_dma_buf_attachment *a = attachment->priv;
	struct ion_buffer *buffer = dmabuf->priv;

	if (buffer->flags & ION_FLAG_CACHED) {
		mutex_lock(&buffer->attachment_lock);
		list_del(&a->list);
		mutex_unlock(&buffer->attachment_lock);
	}

	free_duped_table(a->table);
	kfree(a);
}


static struct sg_table *ion_map_dma_buf(struct dma_buf_attachment *attachment,
					enum dma_data_direction direction)
{
	struct ion_dma_buf_attachment *a = attachment->priv;
	struct sg_table *table;
	int count, map_attrs;
	struct ion_buffer *buffer = attachment->dmabuf->priv;

	table = a->table;

	map_attrs = attachment->dma_map_attrs;
	if (!(buffer->flags & ION_FLAG_CACHED) ||
	    !hlos_accessible_buffer(buffer))
		map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	if (map_attrs & DMA_ATTR_DELAYED_UNMAP) {
		count = msm_dma_map_sg_attrs(attachment->dev, table->sgl,
					     table->nents, direction,
					     attachment->dmabuf, map_attrs);
	} else {
		count = dma_map_sg_attrs(attachment->dev, table->sgl,
					 table->nents, direction,
					 map_attrs);
	}

	if (count <= 0)
		return ERR_PTR(-ENOMEM);

	a->dma_mapped = true;
	return table;
}

static void ion_unmap_dma_buf(struct dma_buf_attachment *attachment,
			      struct sg_table *table,
			      enum dma_data_direction direction)
{
	int map_attrs;
	struct ion_buffer *buffer = attachment->dmabuf->priv;
	struct ion_dma_buf_attachment *a = attachment->priv;

	map_attrs = attachment->dma_map_attrs;
	if (!(buffer->flags & ION_FLAG_CACHED) ||
	    !hlos_accessible_buffer(buffer))
		map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	if (map_attrs & DMA_ATTR_DELAYED_UNMAP)
		msm_dma_unmap_sg_attrs(attachment->dev, table->sgl,
				       table->nents, direction,
				       attachment->dmabuf,
				       map_attrs);
	else
		dma_unmap_sg_attrs(attachment->dev, table->sgl, table->nents,
				   direction, map_attrs);
	a->dma_mapped = false;
}

void ion_pages_sync_for_device(struct device *dev, struct page *page,
			       size_t size, enum dma_data_direction dir)
{
	struct scatterlist sg;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, page, size, 0);
	sg_dma_address(&sg) = page_to_phys(page);
	dma_sync_sg_for_device(dev, &sg, 1, dir);
}

static void ion_vm_open(struct vm_area_struct *vma)
{
	struct ion_buffer *buffer = vma->vm_private_data;
	struct ion_vma_list *vma_list;

	vma_list = kmalloc(sizeof(*vma_list), GFP_KERNEL);
	if (!vma_list)
		return;

	vma_list->vma = vma;

	mutex_lock(&buffer->vma_lock);
	list_add(&vma_list->list, &buffer->vmas);
	mutex_unlock(&buffer->vma_lock);
}

static void ion_vm_close(struct vm_area_struct *vma)
{
	struct ion_buffer *buffer = vma->vm_private_data;
	struct ion_vma_list *vma_list;

	mutex_lock(&buffer->vma_lock);
	list_for_each_entry(vma_list, &buffer->vmas, list) {
		if (vma_list->vma == vma) {
			list_del(&vma_list->list);
			break;
		}
	}
	mutex_unlock(&buffer->vma_lock);

	kfree(vma_list);
}

static const struct vm_operations_struct ion_vma_ops = {
	.open = ion_vm_open,
	.close = ion_vm_close
};

static int ion_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct ion_buffer *buffer = dmabuf->priv;

	if (!buffer->heap->ops->map_user)
		return -EINVAL;

	if (!(buffer->flags & ION_FLAG_CACHED))
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	vma->vm_private_data = buffer;
	vma->vm_ops = &ion_vma_ops;
	ion_vm_open(vma);

	return buffer->heap->ops->map_user(buffer->heap, buffer, vma);
}

static void ion_dma_buf_release(struct dma_buf *dmabuf)
{
	struct ion_buffer *buffer = dmabuf->priv;

	_ion_buffer_destroy(buffer);
}

static void *ion_dma_buf_vmap(struct dma_buf *dmabuf)
{
	struct ion_buffer *buffer = dmabuf->priv;

	if (!buffer->heap->ops->map_kernel)
		return ERR_PTR(-EINVAL);

	return ion_buffer_kmap_get(buffer);
}

static void ion_dma_buf_vunmap(struct dma_buf *dmabuf, void *vaddr)
{
	struct ion_buffer *buffer = dmabuf->priv;

	ion_buffer_kmap_put(buffer);
}

static void *ion_dma_buf_kmap(struct dma_buf *dmabuf, unsigned long offset)
{
	/*
	 * TODO: Once clients remove their hacks where they assume kmap(ed)
	 * addresses are virtually contiguous implement this properly
	 */
	void *vaddr = ion_dma_buf_vmap(dmabuf);

	if (IS_ERR(vaddr))
		return vaddr;

	return vaddr + offset * PAGE_SIZE;
}

static void ion_dma_buf_kunmap(struct dma_buf *dmabuf, unsigned long offset,
			       void *ptr)
{
	/*
	 * TODO: Once clients remove their hacks where they assume kmap(ed)
	 * addresses are virtually contiguous implement this properly
	 */
	ion_dma_buf_vunmap(dmabuf, ptr);
}

static int ion_sgl_sync_range(struct device *dev, struct scatterlist *sgl,
			      unsigned int nents, unsigned long offset,
			      unsigned long length,
			      enum dma_data_direction dir, bool for_cpu)
{
	int i;
	struct scatterlist *sg;
	unsigned int len = 0;
	dma_addr_t sg_dma_addr;

	for_each_sg(sgl, sg, nents, i) {
		if (sg_dma_len(sg) == 0)
			break;

		if (i > 0) {
			pr_warn_ratelimited(
				"Partial cmo only supported with 1 segment\n"
				"is dma_set_max_seg_size being set on dev:%s\n",
				dev_name(dev));
			return -EINVAL;
		}
	}


	for_each_sg(sgl, sg, nents, i) {
		unsigned int sg_offset, sg_left, size = 0;

		if (i == 0)
			sg_dma_addr = sg_dma_address(sg);

		len += sg->length;
		if (len <= offset) {
			sg_dma_addr += sg->length;
			continue;
		}

		sg_left = len - offset;
		sg_offset = sg->length - sg_left;

		size = (length < sg_left) ? length : sg_left;
		if (for_cpu)
			dma_sync_single_range_for_cpu(dev, sg_dma_addr,
						      sg_offset, size, dir);
		else
			dma_sync_single_range_for_device(dev, sg_dma_addr,
							 sg_offset, size, dir);

		offset += size;
		length -= size;
		sg_dma_addr += sg->length;

		if (length == 0)
			break;
	}

	return 0;
}

static int ion_sgl_sync_mapped(struct device *dev, struct scatterlist *sgl,
			       unsigned int nents, struct list_head *vmas,
			       enum dma_data_direction dir, bool for_cpu)
{
	struct ion_vma_list *vma_list;
	int ret = 0;

	list_for_each_entry(vma_list, vmas, list) {
		struct vm_area_struct *vma = vma_list->vma;

		ret = ion_sgl_sync_range(dev, sgl, nents,
					 vma->vm_pgoff * PAGE_SIZE,
					 vma->vm_end - vma->vm_start, dir,
					 for_cpu);
		if (ret)
			break;
	}

	return ret;
}

static int __ion_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					  enum dma_data_direction direction,
					  bool sync_only_mapped)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_dma_buf_attachment *a;
	int ret = 0;

	if (!hlos_accessible_buffer(buffer))
		return -EPERM;

	if (!(buffer->flags & ION_FLAG_CACHED))
		return 0;

	mutex_lock(&buffer->attachment_lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		int tmp = 0;

		if (!a->dma_mapped)
			continue;

		if (sync_only_mapped)
			tmp = ion_sgl_sync_mapped(a->dev, a->table->sgl,
						  a->table->nents,
						  &buffer->vmas,
						  direction, true);
		else
			dma_sync_sg_for_cpu(a->dev, a->table->sgl,
					    a->table->nents, direction);

		if (tmp)
			ret = tmp;
	}
	mutex_unlock(&buffer->attachment_lock);

	return ret;
}

static int __ion_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					enum dma_data_direction direction,
					bool sync_only_mapped)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_dma_buf_attachment *a;
	int ret = 0;

	if (!hlos_accessible_buffer(buffer))
		return -EPERM;

	if (!(buffer->flags & ION_FLAG_CACHED))
		return 0;

	mutex_lock(&buffer->attachment_lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		int tmp = 0;

		if (!a->dma_mapped)
			continue;

		if (sync_only_mapped)
			tmp = ion_sgl_sync_mapped(a->dev, a->table->sgl,
						  a->table->nents,
						  &buffer->vmas, direction,
						  false);
		else
			dma_sync_sg_for_device(a->dev, a->table->sgl,
					       a->table->nents, direction);

		if (tmp)
			ret = tmp;
	}
	mutex_unlock(&buffer->attachment_lock);

	return ret;
}

static int ion_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					enum dma_data_direction direction)
{
	return __ion_dma_buf_begin_cpu_access(dmabuf, direction, false);
}

static int ion_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
				      enum dma_data_direction direction)
{
	return __ion_dma_buf_end_cpu_access(dmabuf, direction, false);
}

static int ion_dma_buf_begin_cpu_access_umapped(struct dma_buf *dmabuf,
						enum dma_data_direction dir)
{
	return __ion_dma_buf_begin_cpu_access(dmabuf, dir, true);
}

static int ion_dma_buf_end_cpu_access_umapped(struct dma_buf *dmabuf,
					      enum dma_data_direction dir)
{
	return __ion_dma_buf_end_cpu_access(dmabuf, dir, true);
}

static int ion_dma_buf_begin_cpu_access_partial(struct dma_buf *dmabuf,
						enum dma_data_direction dir,
						unsigned int offset,
						unsigned int len)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_dma_buf_attachment *a;
	int ret = 0;

	if (!hlos_accessible_buffer(buffer))
		return -EPERM;

	if (!(buffer->flags & ION_FLAG_CACHED))
		return 0;

	mutex_lock(&buffer->attachment_lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		int tmp = 0;

		if (!a->dma_mapped)
			continue;

		ret = ion_sgl_sync_range(a->dev, a->table->sgl, a->table->nents,
					 offset, len, dir, true);

		if (tmp)
			ret = tmp;
	}
	mutex_unlock(&buffer->attachment_lock);

	return ret;
}

static int ion_dma_buf_end_cpu_access_partial(struct dma_buf *dmabuf,
					      enum dma_data_direction direction,
					      unsigned int offset,
					      unsigned int len)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_dma_buf_attachment *a;
	int ret = 0;

	if (!hlos_accessible_buffer(buffer))
		return -EPERM;

	if (!(buffer->flags & ION_FLAG_CACHED))
		return 0;

	mutex_lock(&buffer->attachment_lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		int tmp = 0;

		if (!a->dma_mapped)
			continue;

		ret = ion_sgl_sync_range(a->dev, a->table->sgl, a->table->nents,
					 offset, len, direction, false);

		if (tmp)
			ret = tmp;
	}
	mutex_unlock(&buffer->attachment_lock);

	return ret;
}

static int ion_dma_buf_get_flags(struct dma_buf *dmabuf,
				 unsigned long *flags)
{
	struct ion_buffer *buffer = dmabuf->priv;

	*flags = buffer->flags;
	return 0;
}

static const struct dma_buf_ops dma_buf_ops = {
	.map_dma_buf = ion_map_dma_buf,
	.unmap_dma_buf = ion_unmap_dma_buf,
	.mmap = ion_mmap,
	.release = ion_dma_buf_release,
	.attach = ion_dma_buf_attach,
	.detach = ion_dma_buf_detach,
	.begin_cpu_access = ion_dma_buf_begin_cpu_access,
	.end_cpu_access = ion_dma_buf_end_cpu_access,
	.begin_cpu_access_umapped = ion_dma_buf_begin_cpu_access_umapped,
	.end_cpu_access_umapped = ion_dma_buf_end_cpu_access_umapped,
	.begin_cpu_access_partial = ion_dma_buf_begin_cpu_access_partial,
	.end_cpu_access_partial = ion_dma_buf_end_cpu_access_partial,
	.map_atomic = ion_dma_buf_kmap,
	.unmap_atomic = ion_dma_buf_kunmap,
	.map = ion_dma_buf_kmap,
	.unmap = ion_dma_buf_kunmap,
	.vmap = ion_dma_buf_vmap,
	.vunmap = ion_dma_buf_vunmap,
	.get_flags = ion_dma_buf_get_flags
};

struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
			  unsigned int flags)
{
	struct ion_device *dev = internal_dev;
	struct dma_buf_export_info exp_info;
	struct ion_buffer *buffer = NULL;
	struct dma_buf *dmabuf;
	struct ion_heap *heap;

	len = PAGE_ALIGN(len);
	if (!len)
		return ERR_PTR(-EINVAL);

	down_read(&dev->heap_lock);
	plist_for_each_entry(heap, &dev->heaps, node) {
		if (!(BIT(heap->id) & heap_id_mask))
			continue;

		buffer = ion_buffer_create(heap, dev, len, flags);
		if (!IS_ERR(buffer) || PTR_ERR(buffer) == -EINTR)
			break;
	}
	up_read(&dev->heap_lock);

	if (IS_ERR_OR_NULL(buffer))
		return ERR_PTR(-EINVAL);

	exp_info = (typeof(exp_info)){
		.ops = &dma_buf_ops,
		.flags = O_RDWR,
		.size = buffer->size,
		.priv = buffer
	};

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf))
		_ion_buffer_destroy(buffer);

	return dmabuf;
}

int ion_alloc_fd(size_t len, unsigned int heap_id_mask, unsigned int flags)
{
	struct dma_buf *dmabuf;
	int fd;

	dmabuf = ion_alloc(len, heap_id_mask, flags);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	fd = dma_buf_fd(dmabuf, O_CLOEXEC);
	if (fd < 0)
		dma_buf_put(dmabuf);

	return fd;
}

int ion_query_heaps(struct ion_heap_query *query)
{
	struct ion_heap_data __user *ubuf = u64_to_user_ptr(query->heaps);
	struct ion_device *dev = internal_dev;
	struct ion_heap_data hdata;
	struct ion_heap *heap;
	int cnt = 0, max_cnt;

	memset(&hdata, 0, sizeof(hdata));

	if (!ubuf) {
		down_read(&dev->heap_lock);
		query->cnt = dev->heap_cnt;
		up_read(&dev->heap_lock);

		return 0;
	}

	if (query->cnt <= 0)
		return -EINVAL;

	max_cnt = query->cnt;

	down_read(&dev->heap_lock);
	plist_for_each_entry(heap, &dev->heaps, node) {
		strlcpy(hdata.name, heap->name, sizeof(hdata.name));
		hdata.name[sizeof(hdata.name) - 1] = '\0';
		hdata.type = heap->type;
		hdata.heap_id = heap->id;

		if (copy_to_user(&ubuf[cnt], &hdata, sizeof(hdata))) {
			up_read(&dev->heap_lock);
			return -EFAULT;
		}

		cnt++;
		if (cnt >= max_cnt)
			break;
	}
	up_read(&dev->heap_lock);

	query->cnt = cnt;
	return 0;
}

static const struct file_operations ion_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = ion_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ion_ioctl,
#endif
};

void ion_device_add_heap(struct ion_device *dev, struct ion_heap *heap)
{
	spin_lock_init(&heap->free_lock);
	heap->free_list_size = 0;

	if (heap->flags & ION_HEAP_FLAG_DEFER_FREE)
		ion_heap_init_deferred_free(heap);

	if (heap->flags & ION_HEAP_FLAG_DEFER_FREE || heap->ops->shrink)
		ion_heap_init_shrinker(heap);

	heap->dev = dev;
	plist_node_init(&heap->node, -heap->id);

	down_write(&dev->heap_lock);
	plist_add(&heap->node, &dev->heaps);
	dev->heap_cnt++;
	up_write(&dev->heap_lock);
}

struct ion_device *ion_device_create(void)
{
	struct ion_device *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	ion_sg_table_pool = KMEM_CACHE(sg_table, SLAB_HWCACHE_ALIGN);
	if (!ion_sg_table_pool)
		goto free_dev;

	dev->dev.minor = MISC_DYNAMIC_MINOR;
	dev->dev.name = "ion";
	dev->dev.fops = &ion_fops;
	dev->dev.parent = NULL;
	ret = misc_register(&dev->dev);
	if (ret)
		goto free_table_pool;

	init_rwsem(&dev->heap_lock);
	plist_head_init(&dev->heaps);
	internal_dev = dev;
	return dev;

free_table_pool:
	kmem_cache_destroy(ion_sg_table_pool);
free_dev:
	kfree(dev);
	return ERR_PTR(-ENOMEM);
}
