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

#ifndef __QDMA_CDEV_H__
#define __QDMA_CDEV_H__

#include <linux/cdev.h>
#include "version.h"
#include <linux/spinlock_types.h>

#include "libqdma/libqdma_export.h"
#include <linux/workqueue.h>

#define QDMA_CDEV_CLASS_NAME  DRV_MODULE_NAME

#define QDMA_MINOR_MAX (2048)

/* per pci device control */
struct qdma_cdev_cb {
	struct xlnx_pci_dev *xpdev;
	spinlock_t lock;
	int cdev_major;
	int cdev_minor_cnt;
};

struct qdma_cdev {
	struct list_head list_head;
	int minor;
	dev_t cdevno;

	struct qdma_cdev_cb *xcb;
	struct device *sys_device;
	struct cdev cdev;
	unsigned long c2h_qhndl;
	unsigned long h2c_qhndl;
	unsigned short dir_init;
	struct workqueue_struct *aio_wq;
	spinlock_t c2h_lock;
	spinlock_t h2c_lock;

	int (*fp_open_extra)(struct qdma_cdev *);
	int (*fp_close_extra)(struct qdma_cdev *);
	long (*fp_ioctl_extra)(struct qdma_cdev *, unsigned int, unsigned long);
	ssize_t (*fp_rw)(unsigned long dev_hndl, unsigned long qhndl,
			struct qdma_request *);
	char name[0];
};

struct qdma_io_cb {
	void __user *buf;
	size_t len;
	unsigned int pages_nr;
	struct qdma_sw_sg *sgl;
	struct page **pages;

	struct qdma_request req;
};

void qdma_cdev_destroy(struct qdma_cdev *xcdev);
int qdma_cdev_create(struct qdma_cdev_cb *xcb, struct pci_dev *pdev,
			struct qdma_queue_conf *qconf, unsigned int minor,
			unsigned long qhndl, struct qdma_cdev **xcdev_pp,
			char *ebuf, int ebuflen);
void qdma_cdev_device_cleanup(struct qdma_cdev_cb *);
int qdma_cdev_device_init(struct qdma_cdev_cb *);

void qdma_cdev_cleanup(void);
int qdma_cdev_init(void);

#endif /* ifndef __QDMA_CDEV_H__ */
