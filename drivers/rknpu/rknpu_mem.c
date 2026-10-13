// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/version.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
#include <linux/dma-map-ops.h>
#endif

#include "rknpu_drv.h"
#include "rknpu_ioctl.h"
#include "rknpu_mem.h"

#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP

/* Built-in coherent dmabuf exporter: dma_alloc_coherent on the NPU device
 * (IOMMU-aware) wrapped with dma_buf_export. 5.10 has no dma_heap_find(),
 * so we do not depend on the heap subsystem at all.
 */
struct rknpu_cma_buf {
	struct device *dev;
	size_t size;
	void *vaddr;
	dma_addr_t dma_addr;
};

static int rknpu_cma_attach(struct dma_buf *dbuf,
			    struct dma_buf_attachment *attachment)
{
	return 0;
}

static void rknpu_cma_detach(struct dma_buf *dbuf,
			     struct dma_buf_attachment *attachment)
{
}

static struct sg_table *rknpu_cma_map(struct dma_buf_attachment *attachment,
				      enum dma_data_direction dir)
{
	struct rknpu_cma_buf *b = attachment->dmabuf->priv;
	struct sg_table *sgt;
	struct scatterlist *sg;
	int i, ret;
	dma_addr_t da;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = dma_get_sgtable(b->dev, sgt, b->vaddr, b->dma_addr, b->size);
	if (ret) {
		kfree(sgt);
		return ERR_PTR(ret);
	}

	da = b->dma_addr;
	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		sg_dma_address(sg) = da;
		sg_dma_len(sg) = sg->length;
		da += sg->length;
	}

	return sgt;
}

static void rknpu_cma_unmap(struct dma_buf_attachment *attachment,
			    struct sg_table *sgt,
			    enum dma_data_direction dir)
{
	sg_free_table(sgt);
	kfree(sgt);
}

static int rknpu_cma_mmap(struct dma_buf *dbuf, struct vm_area_struct *vma)
{
	struct rknpu_cma_buf *b = dbuf->priv;

	return dma_mmap_coherent(b->dev, vma, b->vaddr, b->dma_addr,
				 vma->vm_end - vma->vm_start);
}

static void rknpu_cma_release(struct dma_buf *dbuf)
{
	struct rknpu_cma_buf *b = dbuf->priv;

	dma_free_coherent(b->dev, b->size, b->vaddr, b->dma_addr);
	kfree(b);
}

static const struct dma_buf_ops rknpu_cma_dmabuf_ops = {
	.attach = rknpu_cma_attach,
	.detach = rknpu_cma_detach,
	.map_dma_buf = rknpu_cma_map,
	.unmap_dma_buf = rknpu_cma_unmap,
	.mmap = rknpu_cma_mmap,
	.release = rknpu_cma_release,
};

static struct dma_buf *rknpu_cma_alloc(struct device *dev, size_t size)
{
	struct rknpu_cma_buf *b;
	struct dma_buf *dbuf;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	b = kzalloc(sizeof(*b), GFP_KERNEL);
	if (!b)
		return ERR_PTR(-ENOMEM);

	b->dev = dev;
	b->size = PAGE_ALIGN(size);
	b->vaddr = dma_alloc_coherent(dev, b->size, &b->dma_addr, GFP_KERNEL);
	if (!b->vaddr) {
		kfree(b);
		return ERR_PTR(-ENOMEM);
	}

	exp_info.ops = &rknpu_cma_dmabuf_ops;
	exp_info.size = b->size;
	exp_info.priv = b;
	exp_info.flags = O_RDWR | O_CLOEXEC;

	dbuf = dma_buf_export(&exp_info);
	if (IS_ERR(dbuf)) {
		dma_free_coherent(dev, b->size, b->vaddr, b->dma_addr);
		kfree(b);
		return ERR_CAST(dbuf);
	}

	return dbuf;
}

/* Scatter (NON_CONTIGUOUS / iommu) exporter: one alloc_page per page,
 * sg_alloc_table_from_pages + dma_map_sgtable for the iommu mapping,
 * wrapped with dma_buf_export. mmap via vm_map_pages.
 */
struct rknpu_sc_buf {
	struct device *dev;
	size_t size;
	int npages;
	struct page **pages;
	struct sg_table sgt;
};

static int rknpu_sc_attach(struct dma_buf *dbuf,
			   struct dma_buf_attachment *attachment)
{
	return 0;
}

static void rknpu_sc_detach(struct dma_buf *dbuf,
			    struct dma_buf_attachment *attachment)
{
}

static struct sg_table *rknpu_sc_map(struct dma_buf_attachment *attachment,
				     enum dma_data_direction dir)
{
	struct rknpu_sc_buf *b = attachment->dmabuf->priv;

	pr_err("RKNPU-DBG sc_map: nents=%d orig_nents=%d first_iova=%pad len=%u\n",
		b->sgt.nents, b->sgt.orig_nents,
		&sg_dma_address(b->sgt.sgl), (unsigned int)sg_dma_len(b->sgt.sgl));
	return &b->sgt;
}

static void rknpu_sc_unmap(struct dma_buf_attachment *attachment,
			   struct sg_table *sgt,
			   enum dma_data_direction dir)
{
}

static int rknpu_sc_mmap(struct dma_buf *dbuf, struct vm_area_struct *vma)
{
	struct rknpu_sc_buf *b = dbuf->priv;

	if (vma->vm_pgoff != 0)
		return -EINVAL;

	return vm_map_pages(vma, b->pages, b->npages);
}

static void rknpu_sc_release(struct dma_buf *dbuf)
{
	struct rknpu_sc_buf *b = dbuf->priv;
	int i;

	dma_unmap_sgtable(b->dev, &b->sgt, DMA_BIDIRECTIONAL, 0);
	sg_free_table(&b->sgt);
	for (i = 0; i < b->npages; i++)
		__free_page(b->pages[i]);
	kfree(b->pages);
	kfree(b);
}

static const struct dma_buf_ops rknpu_sc_dmabuf_ops = {
	.attach = rknpu_sc_attach,
	.detach = rknpu_sc_detach,
	.map_dma_buf = rknpu_sc_map,
	.unmap_dma_buf = rknpu_sc_unmap,
	.mmap = rknpu_sc_mmap,
	.release = rknpu_sc_release,
};

static struct dma_buf *rknpu_sc_alloc(struct device *dev, size_t size)
{
	struct rknpu_sc_buf *b;
	struct dma_buf *dbuf;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	int i, ret;

	b = kzalloc(sizeof(*b), GFP_KERNEL);
	if (!b)
		return ERR_PTR(-ENOMEM);

	b->dev = dev;
	b->size = PAGE_ALIGN(size);
	b->npages = b->size >> PAGE_SHIFT;

	b->pages = kcalloc(b->npages, sizeof(*b->pages), GFP_KERNEL);
	if (!b->pages) {
		kfree(b);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < b->npages; i++) {
		b->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!b->pages[i]) {
			while (--i >= 0)
				__free_page(b->pages[i]);
			kfree(b->pages);
			kfree(b);
			return ERR_PTR(-ENOMEM);
		}
	}

	ret = sg_alloc_table_from_pages(&b->sgt, b->pages, b->npages, 0,
					b->size, GFP_KERNEL);
	if (ret) {
		for (i = 0; i < b->npages; i++)
			__free_page(b->pages[i]);
		kfree(b->pages);
		kfree(b);
		return ERR_PTR(ret);
	}

	ret = dma_map_sgtable(dev, &b->sgt, DMA_BIDIRECTIONAL, 0);
	pr_err("RKNPU-DBG sc_alloc: npages=%d nents=%d orig_nents=%d map_ret=%d first_iova=%pad len=%u\n",
		b->npages, b->sgt.nents, b->sgt.orig_nents, ret,
		&sg_dma_address(b->sgt.sgl), (unsigned int)sg_dma_len(b->sgt.sgl));
	if (ret) {
		sg_free_table(&b->sgt);
		for (i = 0; i < b->npages; i++)
			__free_page(b->pages[i]);
		kfree(b->pages);
		kfree(b);
		return ERR_PTR(ret);
	}

	exp_info.ops = &rknpu_sc_dmabuf_ops;
	exp_info.size = b->size;
	exp_info.priv = b;
	exp_info.flags = O_RDWR | O_CLOEXEC;

	dbuf = dma_buf_export(&exp_info);
	if (IS_ERR(dbuf)) {
		dma_unmap_sgtable(dev, &b->sgt, DMA_BIDIRECTIONAL, 0);
		sg_free_table(&b->sgt);
		for (i = 0; i < b->npages; i++)
			__free_page(b->pages[i]);
		kfree(b->pages);
		kfree(b);
		return ERR_CAST(dbuf);
	}

	return dbuf;
}


int rknpu_mem_create_ioctl(struct rknpu_device *rknpu_dev, struct file *file,
			   unsigned int cmd, unsigned long data)
{
	struct rknpu_mem_create args;
	int ret = -EINVAL;
	struct dma_buf_attachment *attachment;
	struct sg_table *table;
	struct scatterlist *sgl;
	dma_addr_t phys;
	struct dma_buf *dmabuf;
	struct page **pages;
	struct page *page;
	struct rknpu_mem_object *rknpu_obj = NULL;
	struct rknpu_session *session = NULL;
	int i, fd;
	unsigned int length, page_count;
	unsigned int in_size = _IOC_SIZE(cmd);
	unsigned int k_size = sizeof(struct rknpu_mem_create);
	char *k_data = (char *)&args;

	if (unlikely(copy_from_user(&args, (struct rknpu_mem_create *)data,
				    in_size))) {
		LOG_ERROR("%s: copy_from_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	if (k_size > in_size)
		memset(k_data + in_size, 0, k_size - in_size);

	/* iommu-mode runtimes ask for NON_CONTIGUOUS buffers:
	 * serve them with the scatter exporter instead of failing. */

	rknpu_obj = kzalloc(sizeof(*rknpu_obj), GFP_KERNEL);
	if (!rknpu_obj)
		return -ENOMEM;

	if (args.handle > 0) {
		fd = args.handle;

		dmabuf = dma_buf_get(fd);
		if (IS_ERR(dmabuf)) {
			ret = PTR_ERR(dmabuf);
			goto err_free_obj;
		}

		rknpu_obj->dmabuf = dmabuf;
		rknpu_obj->owner = 0;
	} else {
		/* Start test kernel alloc/free dma buf */
		if (args.flags & RKNPU_MEM_NON_CONTIGUOUS)
			dmabuf = rknpu_sc_alloc(rknpu_dev->dev, args.size);
		else
			dmabuf = rknpu_cma_alloc(rknpu_dev->dev, args.size);
		if (IS_ERR(dmabuf)) {
			LOG_ERROR("dmabuf alloc failed, args.size = %llu\n",
				  args.size);
			ret = PTR_ERR(dmabuf);
			goto err_free_obj;
		}

		rknpu_obj->dmabuf = dmabuf;
		rknpu_obj->owner = 1;

		fd = dma_buf_fd(dmabuf, O_CLOEXEC | O_RDWR);
		if (fd < 0) {
			LOG_ERROR("dmabuf fd get failed\n");
			ret = -EFAULT;
			goto err_free_dma_buf;
		}
	}

	attachment = dma_buf_attach(dmabuf, rknpu_dev->dev);
	if (IS_ERR(attachment)) {
		LOG_ERROR("dma_buf_attach failed\n");
		ret = PTR_ERR(attachment);
		goto err_free_dma_buf;
	}

	table = dma_buf_map_attachment(attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR(table)) {
		LOG_ERROR("dma_buf_attach failed\n");
		dma_buf_detach(dmabuf, attachment);
		ret = PTR_ERR(table);
		goto err_free_dma_buf;
	}

	for_each_sgtable_dma_sg(table, sgl, i) {
		phys = sg_dma_address(sgl);
		page = sg_page(sgl);
		length = sg_dma_len(sgl);
		LOG_DEBUG("%s, %d, phys: %pad, length: %u\n", __func__,
			  __LINE__, &phys, length);
	}

	if (args.flags & RKNPU_MEM_KERNEL_MAPPING) {
		page_count = length >> PAGE_SHIFT;
		pages = vmalloc(page_count * sizeof(struct page));
		if (!pages) {
			LOG_ERROR("alloc pages failed\n");
			ret = -ENOMEM;
			goto err_detach_dma_buf;
		}

		for (i = 0; i < page_count; i++)
			pages[i] = &page[i];

		rknpu_obj->kv_addr =
			vmap(pages, page_count, VM_MAP, PAGE_KERNEL);
		if (!rknpu_obj->kv_addr) {
			LOG_ERROR("vmap pages addr failed\n");
			ret = -ENOMEM;
			goto err_free_pages;
		}
		vfree(pages);
		pages = NULL;
	}

	pr_err("RKNPU-DBG fill: table=%p nents=%d orig=%d sgl=%p sgl_iova=%pad phys=%pad len=%u flags=%u\n",
		table, table->nents, table->orig_nents, table->sgl,
		&sg_dma_address(table->sgl), &phys, length, args.flags);
	rknpu_obj->size = PAGE_ALIGN(args.size);
	rknpu_obj->dma_addr = phys;
	rknpu_obj->sgt = table;
	pr_err("RKNPU-DBG create-tail: obj=%p sgt=%p sgl=%p nents=%d orig=%d dmabuf=%p owner=%u\n",
		rknpu_obj, table, table->sgl, table->nents,
		table->orig_nents, dmabuf, rknpu_obj->owner);

	args.size = rknpu_obj->size;
	args.obj_addr = (__u64)(uintptr_t)rknpu_obj;
	args.dma_addr = rknpu_obj->dma_addr;
	args.handle = fd;

	LOG_DEBUG(
		"args.handle: %d, args.size: %lld, rknpu_obj: %#llx, rknpu_obj->dma_addr: %#llx\n",
		args.handle, args.size, (__u64)(uintptr_t)rknpu_obj,
		(__u64)rknpu_obj->dma_addr);

	if (unlikely(copy_to_user((struct rknpu_mem_create *)data, &args,
				  in_size))) {
		LOG_ERROR("%s: copy_to_user failed\n", __func__);
		ret = -EFAULT;
		goto err_unmap_kv_addr;
	}

	rknpu_obj->attachment = attachment;

	spin_lock(&rknpu_dev->lock);

	session = file->private_data;
	if (!session) {
		spin_unlock(&rknpu_dev->lock);
		ret = -EFAULT;
		goto err_unmap_kv_addr;
	}
	list_add_tail(&rknpu_obj->head, &session->list);

	spin_unlock(&rknpu_dev->lock);

	return 0;

err_unmap_kv_addr:
	vunmap(rknpu_obj->kv_addr);
	rknpu_obj->kv_addr = NULL;

err_free_pages:
	vfree(pages);
	pages = NULL;

err_detach_dma_buf:
	dma_buf_unmap_attachment(attachment, table, DMA_BIDIRECTIONAL);
	dma_buf_detach(dmabuf, attachment);

err_free_dma_buf:
	if (rknpu_obj->owner)
		dma_buf_put(dmabuf);
	else
		dma_buf_put(dmabuf);

err_free_obj:
	kfree(rknpu_obj);

	return ret;
}

int rknpu_mem_destroy_ioctl(struct rknpu_device *rknpu_dev, struct file *file,
			    unsigned long data)
{
	struct rknpu_mem_object *rknpu_obj, *entry, *q;
	struct rknpu_session *session = NULL;
	struct rknpu_mem_destroy args;
	int ret = -EFAULT;

	if (unlikely(copy_from_user(&args, (struct rknpu_mem_destroy *)data,
				    sizeof(struct rknpu_mem_destroy)))) {
		LOG_ERROR("%s: copy_from_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	if (!kern_addr_valid(args.obj_addr)) {
		LOG_ERROR("%s: invalid obj_addr: %#llx\n", __func__,
			  (__u64)(uintptr_t)args.obj_addr);
		ret = -EINVAL;
		return ret;
	}

	rknpu_obj = (struct rknpu_mem_object *)(uintptr_t)args.obj_addr;
	LOG_DEBUG(
		"free args.handle: %d, rknpu_obj: %#llx, rknpu_obj->dma_addr: %#llx\n",
		args.handle, (__u64)(uintptr_t)rknpu_obj,
		(__u64)rknpu_obj->dma_addr);

	spin_lock(&rknpu_dev->lock);
	session = file->private_data;
	if (!session) {
		spin_unlock(&rknpu_dev->lock);
		ret = -EFAULT;
		return ret;
	}
	list_for_each_entry_safe(entry, q, &session->list, head) {
		if (entry == rknpu_obj) {
			list_del(&entry->head);
			break;
		}
	}
	spin_unlock(&rknpu_dev->lock);

	if (rknpu_obj == entry) {
		vunmap(rknpu_obj->kv_addr);
		rknpu_obj->kv_addr = NULL;

		if (rknpu_obj->attachment) {
			dma_buf_unmap_attachment(rknpu_obj->attachment,
						 rknpu_obj->sgt,
						 DMA_BIDIRECTIONAL);
			dma_buf_detach(rknpu_obj->dmabuf,
					       rknpu_obj->attachment);
		}

		if (!rknpu_obj->owner)
			dma_buf_put(rknpu_obj->dmabuf);

		kfree(rknpu_obj);
	}

	return 0;
}

/*
 * begin cpu access => for_cpu = true
 * end cpu access => for_cpu = false
 */
static void __maybe_unused rknpu_dma_buf_sync(
	struct rknpu_device *rknpu_dev, struct rknpu_mem_object *rknpu_obj,
	u32 offset, u32 length, enum dma_data_direction dir, bool for_cpu)
{
	struct device *dev = rknpu_dev->dev;
	struct sg_table *sgt = rknpu_obj->sgt;
	struct scatterlist *sg;
	dma_addr_t sg_dma_addr;
	unsigned int len = 0;
	int i;

	pr_err("RKNPU-DBG sync: obj=%p sgt=%p sgl=%p nents=%d orig=%d dmabuf=%p flags=%lx off=%u len=%u\n",
		rknpu_obj, sgt, sgt ? sgt->sgl : NULL,
		sgt ? sgt->nents : -1, sgt ? sgt->orig_nents : -1,
		rknpu_obj->dmabuf, rknpu_obj->flags, offset, length);
	if (!sgt || !sgt->sgl || !sgt->nents) {
		LOG_ERROR("%s: bad sgt %p sgl, using dmabuf cpu access\n",
			__func__, sgt);
		if (for_cpu)
			dma_buf_begin_cpu_access(rknpu_obj->dmabuf, dir);
		else
			dma_buf_end_cpu_access(rknpu_obj->dmabuf, dir);
		return;
	}
	sg = sgt->sgl;
	sg_dma_addr = sg_dma_address(sg);

	for_each_sgtable_dma_sg(sgt, sg, i) {
		unsigned int sg_offset, sg_left, size = 0;
		unsigned int sg_len = sg_dma_len(sg);

		len += sg_len;
		if (len <= offset) {
			sg_dma_addr += sg_len;
			continue;
		}

		sg_left = len - offset;
		sg_offset = sg_len - sg_left;

		size = (length < sg_left) ? length : sg_left;

		if (for_cpu)
			dma_sync_single_range_for_cpu(dev, sg_dma_addr,
						      sg_offset, size, dir);
		else
			dma_sync_single_range_for_device(dev, sg_dma_addr,
							 sg_offset, size, dir);

		offset += size;
		length -= size;
		sg_dma_addr += sg_len;

		if (length == 0)
			break;
	}
}

int rknpu_mem_sync_ioctl(struct rknpu_device *rknpu_dev, unsigned long data)
{
	struct rknpu_mem_object *rknpu_obj = NULL;
	struct rknpu_mem_sync args;
#ifdef CONFIG_DMABUF_PARTIAL
	struct dma_buf *dmabuf;
#endif
	int ret = -EFAULT;

	if (unlikely(copy_from_user(&args, (struct rknpu_mem_sync *)data,
				    sizeof(struct rknpu_mem_sync)))) {
		LOG_ERROR("%s: copy_from_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	if (!kern_addr_valid(args.obj_addr)) {
		LOG_ERROR("%s: invalid obj_addr: %#llx\n", __func__,
			  (__u64)(uintptr_t)args.obj_addr);
		ret = -EINVAL;
		return ret;
	}

	rknpu_obj = (struct rknpu_mem_object *)(uintptr_t)args.obj_addr;

#ifndef CONFIG_DMABUF_PARTIAL
	if (args.flags & RKNPU_MEM_SYNC_TO_DEVICE) {
		rknpu_dma_buf_sync(rknpu_dev, rknpu_obj, args.offset, args.size,
				   DMA_TO_DEVICE, false);
	}
	if (args.flags & RKNPU_MEM_SYNC_FROM_DEVICE) {
		rknpu_dma_buf_sync(rknpu_dev, rknpu_obj, args.offset, args.size,
				   DMA_FROM_DEVICE, true);
	}
#else
	dmabuf = rknpu_obj->dmabuf;
	if (args.flags & RKNPU_MEM_SYNC_TO_DEVICE) {
		dmabuf->ops->end_cpu_access_partial(dmabuf, DMA_TO_DEVICE,
						    args.offset, args.size);
	}
	if (args.flags & RKNPU_MEM_SYNC_FROM_DEVICE) {
		dmabuf->ops->begin_cpu_access_partial(dmabuf, DMA_FROM_DEVICE,
						      args.offset, args.size);
	}
#endif

	return 0;
}

#endif
