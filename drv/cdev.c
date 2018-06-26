/*
 * This file is part of the Xilinx DMA IP Core driver for Linux
 *
 * Copyright (c) 2017-present,  Xilinx, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ":%s: " fmt, __func__

#include "cdev.h"

#include <asm/cacheflush.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/aio.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)
#include <linux/uio.h>
#endif

#include "qdma_mod.h"

struct cdev_async_io {
	struct kiocb *iocb;
	struct qdma_io_cb qiocb;
	bool write;
	bool cancel;
	spinlock_t lock;
	struct work_struct wrk_itm;
	struct cdev_async_io *next;
};

struct class *qdma_class;
static struct kmem_cache *cdev_cache;

static ssize_t cdev_gen_read_write(struct file *file, char __user *buf,
		size_t count, loff_t *pos, bool write);
static void unmap_user_buf(struct qdma_io_cb *iocb, bool write);
static inline void iocb_release(struct qdma_io_cb *iocb);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,0)
static int caio_cancel(struct kiocb *iocb)
#else
static int caio_cancel(struct kiocb *iocb, struct io_event *e)
#endif
{
	struct cdev_async_io *caio = iocb->private;

	spin_lock(&caio->lock);
	caio->cancel = true;
	spin_unlock(&caio->lock);

	return 0;
}

static void requeue_aio_work(struct cdev_async_io *caio)
{
	struct qdma_cdev *xcdev;

	xcdev = (struct qdma_cdev *)caio->iocb->ki_filp->private_data;
	if (NULL == xcdev) {
		pr_err("Invalid xcdev\n");
		return;
	}

	queue_work(xcdev->aio_wq, &caio->wrk_itm);
}

static void async_io_handler(struct work_struct *work)
{
	struct qdma_cdev *xcdev;
	struct cdev_async_io *caio = container_of(work, struct cdev_async_io, wrk_itm);
	ssize_t numbytes = 0;
	int lock_stat;
	unsigned long qhndl;
	spinlock_t *aio_lock;

	if (NULL == caio) {
		pr_err("Invalid work struct\n");
		return;
	}
	/* Safeguarding for cancel requests */
	lock_stat = spin_trylock(&caio->lock);
	if (!lock_stat) {
		pr_warn("caio lock not acquired\n");
		goto requeue;
	}

	if (false != caio->cancel) {
		pr_err("skipping aio\n");
		goto skip_tran;
	}

	xcdev = (struct qdma_cdev *)caio->iocb->ki_filp->private_data;
	if (NULL == xcdev) {
		pr_err("Invalid xcdev\n");
		return;
	}
	aio_lock = caio->write ? &xcdev->h2c_lock : &xcdev->c2h_lock;
	/* use try_lock so that kthread_stop is handled ASAP */
	lock_stat = spin_trylock(aio_lock);
	if (!lock_stat) {
		spin_unlock(&caio->lock);
		goto requeue; /* allowing only 1 aio request per dir of cdev */
	}


	qhndl = caio->write ? xcdev->h2c_qhndl : xcdev->c2h_qhndl;

	numbytes = xcdev->fp_rw(xcdev->xcb->xpdev->dev_hndl,
	                        qhndl,
				&(caio->qiocb.req));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
	caio->iocb->ki_complete(caio->iocb, numbytes, 0);
#else
	aio_complete(caio->iocb, numbytes, 0);
#endif
	unmap_user_buf(&(caio->qiocb), caio->write);
	iocb_release(&caio->qiocb);
	spin_unlock(aio_lock);

skip_tran:
	spin_unlock(&caio->lock);
	kmem_cache_free(cdev_cache, caio); /* how do we handle if simultaneous free req comes from cdev destroy? */
	return;
requeue:
	requeue_aio_work(caio);
}

/*
 * character device file operations
 */
static int cdev_gen_open(struct inode *inode, struct file *file)
{
	struct qdma_cdev *xcdev = container_of(inode->i_cdev, struct qdma_cdev,
						cdev);
	file->private_data = xcdev;

	if (xcdev->fp_open_extra)
		return xcdev->fp_open_extra(xcdev);

	return 0;
}

static int cdev_gen_close(struct inode *inode, struct file *file)
{
	struct qdma_cdev *xcdev = (struct qdma_cdev *)file->private_data;

	if (xcdev->fp_close_extra)
		return xcdev->fp_close_extra(xcdev);

	return 0;
}

static loff_t cdev_gen_llseek(struct file *file, loff_t off, int whence)
{
	struct qdma_cdev *xcdev = (struct qdma_cdev *)file->private_data;

	loff_t newpos = 0;

	switch (whence) {
	case 0: /* SEEK_SET */
		newpos = off;
		break;
	case 1: /* SEEK_CUR */
		newpos = file->f_pos + off;
		break;
	case 2: /* SEEK_END, @TODO should work from end of address space */
		newpos = UINT_MAX + off;
		break;
	default: /* can't happen */
		return -EINVAL;
	}
	if (newpos < 0)
		return -EINVAL;
	file->f_pos = newpos;

	pr_debug("%s: pos=%lld\n", xcdev->name, (signed long long)newpos);

	return newpos;
}

static long cdev_gen_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	struct qdma_cdev *xcdev = (struct qdma_cdev *)file->private_data;

	if (xcdev->fp_ioctl_extra)
		return xcdev->fp_ioctl_extra(xcdev, cmd, arg);

	pr_info("%s ioctl NOT supported.\n", xcdev->name);
	return -EINVAL;
}

/*
 * cdev r/w
 */
static inline void iocb_release(struct qdma_io_cb *iocb)
{
	if (iocb->sgl) {
		kfree(iocb->sgl);
		iocb->sgl = NULL;
	}
	if (iocb->pages) {
		kfree(iocb->pages);
		iocb->pages = NULL;
	}
	iocb->buf = NULL;
}

static void unmap_user_buf(struct qdma_io_cb *iocb, bool write)
{
	int i;

	if (iocb->sgl) {
		kfree(iocb->sgl);
		iocb->sgl = NULL;
	}

	if (!iocb->pages || !iocb->pages_nr)
		return;

	for (i = 0; i < iocb->pages_nr; i++) {
		if (iocb->pages[i]) {
			if (!write)
				set_page_dirty_lock(iocb->pages[i]);
			put_page(iocb->pages[i]);
		} else
			break;
	}

	if (i != iocb->pages_nr)
		pr_info("sgl pages %d/%u.\n", i, iocb->pages_nr);

	iocb->pages_nr = 0;
}

static int map_user_buf_to_sgl(struct qdma_io_cb *iocb, bool write)
{
	unsigned long len = iocb->len;
	char *buf = iocb->buf;
	struct qdma_sw_sg *sg;
	unsigned int pages_nr = (((unsigned long)buf + len + PAGE_SIZE - 1) -
				 ((unsigned long)buf & PAGE_MASK))
				>> PAGE_SHIFT;
	int i;
	int rv;

	if (len == 0) pages_nr = 1;
	if (pages_nr == 0)
		return -EINVAL;

	sg = kzalloc(pages_nr * sizeof(struct qdma_sw_sg), GFP_KERNEL);
	if (!sg) {
		pr_err("sgl %u OOM.\n", pages_nr);
		return -ENOMEM;
	}
	iocb->sgl = sg;

	iocb->pages = kmalloc(pages_nr * sizeof(struct page *), GFP_KERNEL);
	if (!iocb->pages) {
		pr_err("pages OOM.\n");
		rv = -ENOMEM;
		goto err_out;
	}
	rv = get_user_pages_fast((unsigned long)buf, pages_nr, 1/* write */,
				iocb->pages);
	/* No pages were pinned */
	if (rv < 0) {
		pr_err("unable to pin down %u user pages, %d.\n",
			pages_nr, rv);
		goto err_out;
	}
	/* Less pages pinned than wanted */
	if (rv != pages_nr) {
		pr_err("unable to pin down all %u user pages, %d.\n",
			pages_nr, rv);
		rv = -EFAULT;
		iocb->pages_nr = rv;
		goto err_out;
	}

	for (i = 1; i < pages_nr; i++) {
		if (iocb->pages[i - 1] == iocb->pages[i]) {
			pr_err("duplicate pages, %d, %d.\n",
				i - 1, i);
			rv = -EFAULT;
			iocb->pages_nr = pages_nr;
			goto err_out;
		}
	}

	sg = iocb->sgl;
	for (i = 0; i < pages_nr; i++, sg++) {
		unsigned int offset = offset_in_page(buf);
		unsigned int nbytes = min_t(unsigned int, PAGE_SIZE - offset,
						len);
		struct page *pg = iocb->pages[i];

		flush_dcache_page(pg);

		sg->next = sg + 1;
		sg->pg = pg;
		sg->offset = offset;
		sg->len = nbytes;
		sg->dma_addr = 0UL;

		buf += nbytes;
		len -= nbytes;
//		pr_info("sg->offset = %x", sg->offset);
	}

	iocb->sgl[pages_nr - 1].next = NULL;
	iocb->pages_nr = pages_nr;
	return 0;

err_out:
	unmap_user_buf(iocb, write);

	return rv;
}

static ssize_t cdev_gen_read_write(struct file *file, char __user *buf,
		size_t count, loff_t *pos, bool write)
{
	struct qdma_cdev *xcdev = (struct qdma_cdev *)file->private_data;
	struct qdma_io_cb iocb;
	struct qdma_request *req = &iocb.req;
	ssize_t res = 0;
	int rv;
	int lock_stat;
	unsigned long qhndl = write ? xcdev->h2c_qhndl : xcdev->c2h_qhndl;
	spinlock_t *xcdev_lock = write ?
			&xcdev->h2c_lock : &xcdev->c2h_lock;

	if (!xcdev) {
		pr_info("file 0x%p, xcdev NULL, 0x%p,%llu, pos %llu, W %d.\n",
			file, buf, (u64)count, (u64)*pos, write);
		return -EINVAL;
	}

	if (!xcdev->fp_rw) {
		pr_info("file 0x%p, %s, NO rw, 0x%p,%llu, pos %llu, W %d.\n",
			file, xcdev->name, buf, (u64)count, (u64)*pos, write);
		return -EINVAL;
	}

	pr_debug("%s, priv 0x%lx: buf 0x%p,%llu, pos %llu, W %d.\n",
		xcdev->name, qhndl, buf, (u64)count, (u64)*pos,
		write);

	/* use try_lock so that kthread_stop is handled ASAP */
	lock_stat = spin_trylock(xcdev_lock);
	if (!lock_stat) return -EINVAL; /* allowing only 1 aio request per cdev */
	memset(&iocb, 0, sizeof(struct qdma_io_cb));
	iocb.buf = buf;
	iocb.len = count;
	rv = map_user_buf_to_sgl(&iocb, write);
	if (rv < 0) {
		spin_unlock(xcdev_lock);
		return rv;
	}

	req->sgcnt = iocb.pages_nr;
	req->sgl = iocb.sgl;
	req->write = write ? 1 : 0;
	req->dma_mapped = 0;
	req->udd_len = 0;
	req->ep_addr = (u64)*pos;
	req->count = count;
	req->timeout_ms = 10 * 1000;	/* 10 seconds */
	req->fp_done = NULL;		/* blocking */

	res = xcdev->fp_rw(xcdev->xcb->xpdev->dev_hndl, qhndl, req);

	unmap_user_buf(&iocb, write);
	iocb_release(&iocb);
	spin_unlock(xcdev_lock);

	return res;
}

static ssize_t cdev_gen_write(struct file *file, const char __user *buf,
				size_t count, loff_t *pos)
{
	return cdev_gen_read_write(file, (char *)buf, count, pos, 1);
}

static ssize_t cdev_gen_read(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{
	return cdev_gen_read_write(file, (char *)buf, count, pos, 0);
}

static ssize_t cdev_aio_write(struct kiocb *iocb, const struct iovec *io,
                              unsigned long count, loff_t pos)
{
	struct qdma_cdev *xcdev = (struct qdma_cdev *)iocb->ki_filp->private_data;
	struct cdev_async_io *caio;
	struct qdma_request *req;
	int rv;
	unsigned long i;

	if (!xcdev) {
		pr_err("file 0x%p, xcdev NULL, %llu, pos %llu, W %d.\n",
		        iocb->ki_filp, (u64)count, (u64)pos, 1);
		return -EINVAL;
	}

	if (!xcdev->fp_rw) {
		pr_err("No Read write handler assigned\n");
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		caio = kmem_cache_alloc(cdev_cache, GFP_KERNEL);
		req = &(caio->qiocb.req);
		memset(&(caio->qiocb), 0, sizeof(struct qdma_io_cb));
		memset(caio, 0, sizeof(struct cdev_async_io));
		caio->qiocb.buf = io[i].iov_base;
		caio->qiocb.len = io[i].iov_len;
		rv = map_user_buf_to_sgl(&(caio->qiocb), true);
		if (rv < 0) {
			kmem_cache_free(cdev_cache, caio);
			return rv;
		}

		req->sgcnt = caio->qiocb.pages_nr;
		req->sgl = caio->qiocb.sgl;
		req->write = true;
		req->dma_mapped = false;
		req->udd_len = 0;
		req->ep_addr = (u64)pos;
		req->count = io->iov_len;
		req->timeout_ms = 10 * 1000;	/* 10 seconds */
		req->fp_done = NULL;		/* blocking */

		iocb->private = caio;
		caio->iocb = iocb;
		caio->write = true;
		caio->cancel = false;
		spin_lock_init(&caio->lock);

		kiocb_set_cancel_fn(caio->iocb, caio_cancel);
		INIT_WORK(&caio->wrk_itm, async_io_handler);
		queue_work(xcdev->aio_wq, &caio->wrk_itm);
	}

	return -EIOCBQUEUED;
}
static ssize_t cdev_aio_read(struct kiocb *iocb, const struct iovec *io,
                             unsigned long count, loff_t pos)
{
	struct qdma_cdev *xcdev = (struct qdma_cdev *)iocb->ki_filp->private_data;
	struct cdev_async_io *caio;
	struct qdma_request *req;
	int rv;
	unsigned long i;

	if (!xcdev) {
		pr_err("file 0x%p, xcdev NULL, %llu, pos %llu, W %d.\n",
		        iocb->ki_filp, (u64)count, (u64)pos, 1);
		return -EINVAL;
	}

	if (!xcdev->fp_rw) {
		pr_err("No Read write handler assigned\n");
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		caio = kmem_cache_alloc(cdev_cache, GFP_KERNEL);
		req = &(caio->qiocb.req);
		memset(&(caio->qiocb), 0, sizeof(struct qdma_io_cb));
		memset(caio, 0, sizeof(struct cdev_async_io));
		caio->qiocb.buf = io[i].iov_base;
		caio->qiocb.len = io[i].iov_len;
		rv = map_user_buf_to_sgl(&(caio->qiocb), false);
		if (rv < 0) {
			kmem_cache_free(cdev_cache, caio);
			return rv;
		}

		req->sgcnt = caio->qiocb.pages_nr;
		req->sgl = caio->qiocb.sgl;
		req->write = false;
		req->dma_mapped = false;
		req->udd_len = 0;
		req->ep_addr = (u64)pos;
		req->count = io->iov_len;
		req->timeout_ms = 10 * 1000;	/* 10 seconds */
		req->fp_done = NULL;		/* blocking */

		iocb->private = caio;
		caio->iocb = iocb;
		caio->write = false;
		caio->cancel = false;
		spin_lock_init(&caio->lock);

		kiocb_set_cancel_fn(caio->iocb, caio_cancel);
		INIT_WORK(&caio->wrk_itm, async_io_handler);
		queue_work(xcdev->aio_wq, &caio->wrk_itm);
	}

	return -EIOCBQUEUED;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)
static ssize_t cdev_write_iter(struct kiocb *iocb, struct iov_iter *io)
{
	return cdev_aio_write(iocb, io->iov, io->nr_segs, io->iov_offset);
}

static ssize_t cdev_read_iter(struct kiocb *iocb, struct iov_iter *io)
{
	return cdev_aio_read(iocb, io->iov, io->nr_segs, io->iov_offset);
}
#endif

static const struct file_operations cdev_gen_fops = {
	.owner = THIS_MODULE,
	.open = cdev_gen_open,
	.release = cdev_gen_close,
	.write = cdev_gen_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)
	.write_iter = cdev_write_iter,
#else
	.aio_write = cdev_aio_write,
#endif
	.read = cdev_gen_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)
	.read_iter = cdev_read_iter,
#else
	.aio_read = cdev_aio_read,
#endif
	.unlocked_ioctl = cdev_gen_ioctl,
	.llseek = cdev_gen_llseek,
};

/*
 * xcb: per pci device character device control info.
 * xcdev: per queue character device
 */
int qdma_cdev_display(void *p, char *buf)
{
	struct qdma_cdev *xcdev = (struct qdma_cdev *)p;

	return sprintf(buf, ", cdev %s", xcdev->name);
}

void qdma_cdev_destroy(struct qdma_cdev *xcdev)
{
	pr_debug("destroying cdev %p", xcdev);
	if (!xcdev) {
		pr_info("xcdev NULL.\n");
		return;
	}

	if (xcdev->aio_wq)
		destroy_workqueue(xcdev->aio_wq);

	if (xcdev->sys_device)
		device_destroy(qdma_class, xcdev->cdevno);

	cdev_del(&xcdev->cdev);

	kfree(xcdev);
}

int qdma_cdev_create(struct qdma_cdev_cb *xcb, struct pci_dev *pdev,
			struct qdma_queue_conf *qconf, unsigned int minor,
			unsigned long qhndl, struct qdma_cdev **xcdev_pp,
			char *ebuf, int ebuflen)
{
	struct qdma_cdev *xcdev;
	int rv;
	unsigned long *priv_data;

	xcdev = kzalloc(sizeof(struct qdma_cdev) + strlen(qconf->name) + 1,
			GFP_KERNEL);
	if (!xcdev) {
		pr_info("%s OOM %lu.\n", qconf->name, sizeof(struct qdma_cdev));
		if (ebuf && ebuflen) {
			rv = sprintf(ebuf, "%s cdev OOM %lu.\n",
				qconf->name, sizeof(struct qdma_cdev));
			ebuf[rv] = '\0';

		}
		return -ENOMEM;
	}

	spin_lock_init(&xcdev->c2h_lock);
	spin_lock_init(&xcdev->h2c_lock);
	xcdev->cdev.owner = THIS_MODULE;
	xcdev->xcb = xcb;
	priv_data = qconf->c2h ? &xcdev->c2h_qhndl : &xcdev->h2c_qhndl;
	*priv_data = qhndl;
	xcdev->dir_init = (1 << qconf->c2h);
	strcpy(xcdev->name, qconf->name);

	xcdev->minor = minor;
	if (xcdev->minor >= xcb->cdev_minor_cnt) {
		pr_info("%s: no char dev. left.\n", qconf->name);
		if (ebuf && ebuflen) {
			rv = sprintf(ebuf, "%s cdev no cdev left.\n",
					qconf->name);
			ebuf[rv] = '\0';
		}
		rv = -ENOSPC;
		goto err_out;
	}
	xcdev->cdevno = MKDEV(xcb->cdev_major, xcdev->minor);

	cdev_init(&xcdev->cdev, &cdev_gen_fops);

	/* bring character device live */
	rv = cdev_add(&xcdev->cdev, xcdev->cdevno, 1);
	if (rv < 0) {
		pr_info("cdev_add failed %d, %s.\n", rv, xcdev->name);
		if (ebuf && ebuflen) {
			int l = sprintf(ebuf, "%s cdev add failed %d.\n",
					qconf->name, rv);
			ebuf[l] = '\0';
		}
		goto err_out;
	}

	/* create device on our class */
	if (qdma_class) {
		xcdev->sys_device = device_create(qdma_class, &(pdev->dev),
				xcdev->cdevno, NULL, "%s", xcdev->name);
		if (IS_ERR(xcdev->sys_device)) {
			rv = PTR_ERR(xcdev->sys_device);
			pr_info("%s device_create failed %d.\n",
				xcdev->name, rv);
			if (ebuf && ebuflen) {
				int l = sprintf(ebuf,
						"%s device_create failed %d.\n",
						qconf->name, rv);
				ebuf[l] = '\0';
			}
			goto del_cdev;
		}
	}

	xcdev->aio_wq = create_workqueue("AIO_WQ");

	xcdev->fp_rw = qdma_request_submit;

	*xcdev_pp = xcdev;
	return 0;

del_cdev:
	cdev_del(&xcdev->cdev);

err_out:
	kfree(xcdev);
	return rv;
}

/*
 * per device initialization & cleanup
 */
void qdma_cdev_device_cleanup(struct qdma_cdev_cb *xcb)
{
	if (!xcb->cdev_major)
		return;

	unregister_chrdev_region(MKDEV(xcb->cdev_major, 0),
				xcb->cdev_minor_cnt);
	xcb->cdev_major = 0;
}

int qdma_cdev_device_init(struct qdma_cdev_cb *xcb)
{
	dev_t dev;
	int rv;

	spin_lock_init(&xcb->lock);
//	INIT_LIST_HEAD(&xcb->cdev_list);

	xcb->cdev_minor_cnt = QDMA_MINOR_MAX;

	if (xcb->cdev_major) {
		pr_warn("major %d already exist.\n", xcb->cdev_major);
		return -EINVAL;
	}

	/* allocate a dynamically allocated char device node */
	rv = alloc_chrdev_region(&dev, 0, xcb->cdev_minor_cnt,
				QDMA_CDEV_CLASS_NAME);
	if (rv) {
		pr_err("unable to allocate cdev region %d.\n", rv);
		return rv;
	}
	xcb->cdev_major = MAJOR(dev);

	return 0;
}

/*
 * driver-wide Initialization & cleanup
 */

int qdma_cdev_init(void)
{
	qdma_class = class_create(THIS_MODULE, QDMA_CDEV_CLASS_NAME);
	if (IS_ERR(qdma_class)) {
		pr_info("%s: failed to create class 0x%lx.",
			QDMA_CDEV_CLASS_NAME, (unsigned long)qdma_class);
		qdma_class = NULL;
		return -1;
	}
	/* using kmem_cache_create to enable sequential cleanup */
	cdev_cache = kmem_cache_create("cdev_cache",
	                               sizeof(struct cdev_async_io),
	                               0,
	                               SLAB_HWCACHE_ALIGN,
	                               NULL);
	if (!cdev_cache) {
		pr_info("memory allocation for cdev_cache failed. OOM\n");
		return -ENOMEM;
	}

	return 0;
}

void qdma_cdev_cleanup(void)
{
	if (cdev_cache)
		kmem_cache_destroy(cdev_cache);
	if (qdma_class)
		class_destroy(qdma_class);

}
