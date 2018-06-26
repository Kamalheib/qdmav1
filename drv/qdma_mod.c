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

#include "qdma_mod.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/vmalloc.h>

/* include early, to verify it depends only on the headers above */
#include "version.h"

static char version[] =
	DRV_MODULE_DESC " v" DRV_MODULE_VERSION "\n";

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION(DRV_MODULE_DESC);
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_LICENSE("Dual BSD/GPL");

/* Module Parameters */
static unsigned int poll_mode_en = 1;
module_param(poll_mode_en, uint, 0644);
MODULE_PARM_DESC(poll_mode_en, "use hw polling instead of interrupts for determining dma transfer completion");

static unsigned int ind_intr_mode = 0;
module_param(ind_intr_mode, uint, 0644);
MODULE_PARM_DESC(ind_intr_mode, "enable interrupt aggregation");

static unsigned int master_pf = 0;
module_param(master_pf, uint, 0644);
MODULE_PARM_DESC(master_pf, "Master PF for setting global CSRs, dflt PF 0");


#include "pci_ids.h"

/*
 * xpdev helper functions
 */
static LIST_HEAD(xpdev_list);
static DEFINE_MUTEX(xpdev_mutex);

/*****************************************************************************/
/**
 * funcname() -  handler to show the qmax configuration value
 *
 * @dev :   PCIe device handle
 * @attr:   qmax configuration value
 * @buf :   buffer to hold the configured value
 *
 * Handler function to show the qmax
 *
 * @note    none
 *
 * Return:  Returns length of the buffer on success, <0 on failure
 *
 *****************************************************************************/
static ssize_t show_qmax(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xlnx_pci_dev *xpdev;
	int len;
	unsigned int qmax = 0;

	if (!pdev)
		return -EINVAL;

	xpdev = (struct xlnx_pci_dev *)dev_get_drvdata(dev);
	if (!xpdev)
		return -EINVAL;

	qmax = qdma_get_qmax(xpdev->dev_hndl);
	len = sprintf(buf, "%d\n", qmax);
	if (len <= 0)
		pr_err("xlnx_pci_dev: Invalid sprintf len: %d\n", len);

	return len;
}
/*****************************************************************************/
/**
 * funcname() -  handler to set the qmax configuration value
 *
 * @dev :   PCIe device handle
 * @attr:   qmax configuration value
 * @buf :   buffer to hold the configured value
 *
 * Handler function to set the qmax
 *
 * @note    none
 *
 * Return:  Returns length of the buffer on success, <0 on failure
 *
 *****************************************************************************/
static ssize_t set_qmax(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xlnx_pci_dev *xpdev;
	unsigned int qmax = 0;
	int err = 0;

	if (!pdev)
		return -EINVAL;

	xpdev = (struct xlnx_pci_dev *)dev_get_drvdata(dev);
	if (!xpdev)
		return -EINVAL;

	err = kstrtoint(buf, 0, &qmax);
	pr_info("GPIO_DEGUG: kstrtoint suceeded res: %d, qmax = %d ", err, qmax);

	err = qdma_set_qmax(xpdev->dev_hndl, qmax);
	return err ? err : count;
}

/*****************************************************************************/
/**
 * funcname() -  handler to show the intr_rngsz configuration value
 *
 * @dev :   PCIe device handle
 * @attr:   intr_rngsz configuration value
 * @buf :   buffer to hold the configured value
 *
 * Handler function to show the intr_rngsz
 *
 * @note    none
 *
 * Return:  Returns length of the buffer on success, <0 on failure
 *
 *****************************************************************************/
static ssize_t show_intr_rngsz(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xlnx_pci_dev *xpdev ;
	int len;
	unsigned int rngsz = 0;

	if (!pdev)
		return -EINVAL;

	xpdev = (struct xlnx_pci_dev *)dev_get_drvdata(dev);
	if (!xpdev)
		return -EINVAL;

	rngsz = qdma_get_intr_rngsz(xpdev->dev_hndl);
	len = sprintf(buf, "%d\n", rngsz);
	if (len <= 0)
		pr_err("xlnx_pci_dev: Invalid sprintf len: %d\n", len);

	return len;
}

/*****************************************************************************/
/**
 * funcname() -  handler to set the intr_rngsz configuration value
 *
 * @dev :   PCIe device handle
 * @attr:   intr_rngsz configuration value
 * @buf :   buffer to hold the configured value
 *
 * Handler function to set the intr_rngsz
 *
 * @note    none
 *
 * Return:  Returns length of the buffer on success, <0 on failure
 *
 *****************************************************************************/
static ssize_t set_intr_rngsz(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xlnx_pci_dev *xpdev;
	unsigned int rngsz = 0;
	int err = 0;

	if (!pdev)
		return -EINVAL;

	xpdev = (struct xlnx_pci_dev *)dev_get_drvdata(dev);
	if (!xpdev)
		return -EINVAL;

	err = kstrtoint(buf, 0, &rngsz);
	pr_info("GPIO_DEGUG: kstrtoint suceeded res: %d, qmax = %d ", err, rngsz);

	err = qdma_set_intr_rngsz(xpdev->dev_hndl, rngsz);
	return err ? err : count;
}
#ifndef __QDMA_VF__
/*****************************************************************************/
/**
 * funcname() -  handler to show the wrb_acc configuration value
 *
 * @dev :   PCIe device handle
 * @attr:   wrb_acc configuration value
 * @buf :   buffer to hold the configured value
 *
 * Handler function to show the wrb_acc
 *
 * @note    none
 *
 * Return:  Returns length of the buffer on success, <0 on failure
 *
 *****************************************************************************/
static ssize_t show_wrb_acc(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xlnx_pci_dev *xpdev ;
	int len;
	unsigned int wrb_acc = 0;

	if (!pdev)
		return -EINVAL;

	xpdev = (struct xlnx_pci_dev *)dev_get_drvdata(dev);
	if (!xpdev)
		return -EINVAL;

	wrb_acc = qdma_get_wrb_acc(xpdev->dev_hndl);
	len = sprintf(buf, "%d\n", wrb_acc);
	if (len <= 0)
		pr_err("show_wrb_acc: Invalid sprintf len: %d\n", len);

	return len;
}

/*****************************************************************************/
/**
 * funcname() -  handler to set the wrb_acc configuration value
 *
 * @dev :   PCIe device handle
 * @attr:   wrb_acc configuration value
 * @buf :   buffer to hold the configured value
 *
 * Handler function to set the wrb_acc
 *
 * @note    none
 *
 * Return:  Returns length of the buffer on success, <0 on failure
 *
 *****************************************************************************/
static ssize_t set_wrb_acc(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xlnx_pci_dev *xpdev;
	unsigned int wrb_acc = 0;
	int err = 0;

	if (!pdev)
		return -EINVAL;

	xpdev = (struct xlnx_pci_dev *)dev_get_drvdata(dev);
	if (!xpdev)
		return -EINVAL;

	err = kstrtoint(buf, 0, &wrb_acc);
	pr_info("set_wrb_acc: kstrtoint suceeded err: %d, wrb_acc = %d ", err, wrb_acc);

	err = qdma_set_wrb_acc(xpdev->dev_hndl, wrb_acc);
	return err ? err : count;
}
#endif
static DEVICE_ATTR(qmax, S_IWUSR | S_IRUGO, show_qmax, set_qmax);
static DEVICE_ATTR(intr_rngsz, S_IWUSR | S_IRUGO, show_intr_rngsz, set_intr_rngsz);
#ifndef __QDMA_VF__
static DEVICE_ATTR(wrb_acc, S_IWUSR | S_IRUGO, show_wrb_acc, set_wrb_acc);
#endif

static struct attribute *pci_device_attrs[] = {
		&dev_attr_qmax.attr,
		&dev_attr_intr_rngsz.attr,
#ifndef __QDMA_VF__
		&dev_attr_wrb_acc.attr,
#endif
		NULL,
};

static struct attribute_group pci_device_attr_group = {
		.name  = "qdma",
		.attrs = pci_device_attrs,

};

static inline void xpdev_list_remove(struct xlnx_pci_dev *xpdev)
{
	mutex_lock(&xpdev_mutex);
	list_del(&xpdev->list_head);
	mutex_unlock(&xpdev_mutex);
}

static inline void xpdev_list_add(struct xlnx_pci_dev *xpdev)
{
	mutex_lock(&xpdev_mutex);
	list_add_tail(&xpdev->list_head, &xpdev_list);
	mutex_unlock(&xpdev_mutex);
}

int xpdev_list_dump(char *buf, int buflen)
{
	struct xlnx_pci_dev *xpdev, *tmp;
	char* cur = buf;
	char* const end = buf + buflen;

	if (!buf || !buflen)
		return -EINVAL;

	mutex_lock(&xpdev_mutex);
	list_for_each_entry_safe(xpdev, tmp, &xpdev_list, list_head) {
		struct pci_dev *pdev;
		struct qdma_dev_conf conf;
		int rv;

		rv = qdma_device_get_config(xpdev->dev_hndl, &conf, NULL, 0);
		if (rv < 0) {
			cur += snprintf(cur, cur - end,
				"ERR! unable to get device config for idx %u\n",
				xpdev->idx);
			if (cur >= end)
				goto handle_truncation;
			break;
		}

		pdev = conf.pdev;

		cur += snprintf(cur, end - cur,
#ifdef __QDMA_VF__
			"qdmavf%u\t%s\tmax QP: %u, %u~%u\n",
#else
			"qdma%u\t%s\tmax QP: %u, %u~%u\n",
#endif
			xpdev->idx, dev_name(&pdev->dev),
			conf.qsets_max, conf.qsets_base,
			conf.qsets_base + conf.qsets_max - 1);
		if (cur >= end)
			goto handle_truncation;
	}
	mutex_unlock(&xpdev_mutex);

	return cur - buf;

handle_truncation:
	pr_warn("ERR! str truncated. req=%lu, avail=%u", cur - buf, buflen);
	*buf='\0';
	return cur - buf;
}

struct xlnx_pci_dev *xpdev_find_by_idx(unsigned int idx, char *buf, int buflen)
{
	struct xlnx_pci_dev *xpdev, *tmp;

	mutex_lock(&xpdev_mutex);
	list_for_each_entry_safe(xpdev, tmp, &xpdev_list, list_head) {
		if (xpdev->idx == idx) {
			mutex_unlock(&xpdev_mutex);
			return xpdev;
		}
	}
	mutex_unlock(&xpdev_mutex);

	if (buf && buflen) {
		snprintf(buf, buflen, "NO device found at index %u!\n", idx);
	}
	return NULL;
}

struct xlnx_qdata *xpdev_queue_get(struct xlnx_pci_dev *xpdev,
			unsigned int qidx, bool c2h, bool check_qhndl,
			char *ebuf, int ebuflen)
{
	struct xlnx_qdata *qdata;
	int len;

	if (qidx >= xpdev->qmax) {
		pr_debug("qdma%d QID %u too big, %u.\n",
			xpdev->idx, qidx, xpdev->qmax);
		if (ebuf && ebuflen) {
			len = sprintf(ebuf, "QID %u too big, %u.\n",
					 qidx, xpdev->qmax);
		}
		return NULL;
	}

	qdata = xpdev->qdata + qidx;
	if (c2h)
		qdata += xpdev->qmax;

	if (check_qhndl && (!qdata->qhndl && !qdata->xcdev)) {
		pr_debug("qdma%d QID %u NOT configured.\n", xpdev->idx, qidx);
		if (ebuf && ebuflen) {
			len = sprintf(ebuf, "QID %u NOT configured.\n", qidx);
		}
		return NULL;
	}

	return qdata;
}

int xpdev_queue_delete(struct xlnx_pci_dev *xpdev, unsigned int qidx, bool c2h,
			char *ebuf, int ebuflen)
{
	struct xlnx_qdata *qdata = xpdev_queue_get(xpdev, qidx, c2h, 1, ebuf,
						ebuflen);
	int rv = 0;

	if (!qdata)
		return -EINVAL;

	spin_lock(&xpdev->cdev_lock);
	qdata->xcdev->dir_init &= ~(1 << (c2h ? 1 : 0));
	if (qdata->xcdev && !qdata->xcdev->dir_init)
		qdma_cdev_destroy(qdata->xcdev);
	spin_unlock(&xpdev->cdev_lock);

	if (qdata->qhndl != QDMA_QUEUE_IDX_INVALID)
		rv = qdma_queue_remove(xpdev->dev_hndl, qdata->qhndl,
					ebuf, ebuflen);
	else
		pr_debug("qidx %u/%u, c2h %d, qhndl invalid.\n",
			qidx, xpdev->qmax, c2h);

	memset(qdata, 0, sizeof(*qdata));
	return rv;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)
static void xpdev_queue_delete_all(struct xlnx_pci_dev *xpdev)
{
	int i;

	for (i = 0; i < xpdev->qmax; i++) {
		xpdev_queue_delete(xpdev, i, 0, NULL, 0);
		xpdev_queue_delete(xpdev, i, 1, NULL, 0);
	}
}
#endif

int xpdev_queue_add(struct xlnx_pci_dev *xpdev, struct qdma_queue_conf *qconf,
			char *ebuf, int ebuflen)
{
	struct xlnx_qdata *qdata;
	struct qdma_cdev *xcdev;
	struct xlnx_qdata *qdata_tmp;
	u8 dir;
	unsigned long qhndl;
	int rv;

	rv = qdma_queue_add(xpdev->dev_hndl, qconf, &qhndl, ebuf, ebuflen);
	if (rv < 0)
		return rv;

	pr_debug("qdma%d idx %u, st %d, c2h %d, added, qhndl 0x%lx.\n",
		xpdev->idx, qconf->qidx, qconf->st, qconf->c2h, qhndl);

	qdata = xpdev_queue_get(xpdev, qconf->qidx, qconf->c2h, 0, ebuf,
				ebuflen);
	if (!qdata) {
		pr_info("q added 0x%lx, get failed, idx 0x%x.\n",
			qhndl, qconf->qidx);
		return rv;
	}

	dir = qconf->c2h ? 0 : 1;
	spin_lock(&xpdev->cdev_lock);
	qdata_tmp = xpdev_queue_get(xpdev, qconf->qidx, dir, 0, NULL, 0);
	if (qdata_tmp) {
		/* only 1 cdev per queue pair */
		if (qdata_tmp->xcdev) {
			unsigned long *priv_data;

			qdata->qhndl = qhndl;
			qdata->xcdev = qdata_tmp->xcdev;
			priv_data = qconf->c2h ? &qdata->xcdev->c2h_qhndl :
					&qdata->xcdev->h2c_qhndl;
			*priv_data = qhndl;
			qdata->xcdev->dir_init |= (1 << qconf->c2h);

			spin_unlock(&xpdev->cdev_lock);
			return 0;
		}
	}
	spin_unlock(&xpdev->cdev_lock);

	/* always create the cdev */
	rv = qdma_cdev_create(&xpdev->cdev_cb, xpdev->pdev, qconf, qconf->qidx,
				qhndl, &xcdev, ebuf, ebuflen);

	qdata->qhndl = qhndl;
	qdata->xcdev = xcdev;

	return rv;
}

static void xpdev_free(struct xlnx_pci_dev *p)
{
	xpdev_list_remove(p);

	if (((unsigned long)p) >= VMALLOC_START &&
	    ((unsigned long)p) < VMALLOC_END)
		vfree(p);
	else
		kfree(p);
}

static struct xlnx_pci_dev *xpdev_alloc(struct pci_dev *pdev, unsigned int qmax)
{
	int sz = sizeof(struct xlnx_pci_dev) +
		qmax * 2 * sizeof(struct xlnx_qdata);
	struct xlnx_pci_dev *xpdev = kzalloc(sz, GFP_KERNEL);

	if (!xpdev) {
		xpdev = vmalloc(sz);
		if (xpdev)
			memset(xpdev, 0, sz);
	}

	if (!xpdev) {
		pr_info("OMM, qmax %u, sz %u.\n", qmax, sz);
		return NULL;
	}
	spin_lock_init(&xpdev->cdev_lock);
	xpdev->pdev = pdev;
	xpdev->qmax = qmax;
	xpdev->idx = 0xFF;

	xpdev_list_add(xpdev);
	return xpdev;
}

static int probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct qdma_dev_conf conf;
	struct xlnx_pci_dev *xpdev = NULL;
	unsigned long dev_hndl;
	int rv;

	pr_info("%s: func 0x%x/0x%x, p/v %d/%d,0x%p.\n",
		dev_name(&pdev->dev), PCI_FUNC(pdev->devfn), QDMA_PF_MAX,
		pdev->is_physfn, pdev->is_virtfn, pdev->physfn);

	memset(&conf, 0, sizeof(conf));
	conf.poll_mode = poll_mode_en;
	conf.intr_agg = ind_intr_mode;
	conf.vf_max = 0;	/* enable via sysfs */

#ifdef __QDMA_VF__
	conf.qsets_max = QDMA_Q_PER_VF_MAX;
#else
	conf.master_pf = PCI_FUNC(pdev->devfn) == master_pf ? 1 :0;
#endif /* #ifdef __QDMA_VF__ */

	conf.intr_rngsz = QDMA_INTR_COAL_RING_SIZE;
	conf.pdev = pdev;

	rv = qdma_device_open(DRV_MODULE_NAME, &conf, &dev_hndl);
	if (rv < 0)
		return rv;

	xpdev = xpdev_alloc(pdev, conf.qsets_max);
	if (!xpdev) {
		rv = -EINVAL;
		goto close_device;
	}
	xpdev->dev_hndl = dev_hndl;
	xpdev->idx = conf.idx;

	rv = qdma_cdev_device_init(&xpdev->cdev_cb);
	if (rv < 0)
		goto close_device;
	xpdev->cdev_cb.xpdev = xpdev;

	/* Create the files for attributes in sysfs */
	rv = sysfs_create_group(&pdev->dev.kobj, &pci_device_attr_group);
	if (rv < 0)
		goto close_device;

	dev_set_drvdata(&pdev->dev, xpdev);

	return 0;

close_device:
	qdma_device_close(pdev, dev_hndl);

	if (xpdev)
		xpdev_free(xpdev);

	return rv;
}

static void xpdev_device_cleanup(struct xlnx_pci_dev *xpdev)
{
	struct xlnx_qdata* qdata = xpdev->qdata;
	struct xlnx_qdata* qmax = qdata + (xpdev->qmax * 2); /* h2c and c2h */

	spin_lock(&xpdev->cdev_lock);
	for (; qdata != qmax; qdata++) {
		if (qdata->xcdev) {
			/* if either h2c(1) or c2h(2) bit set, but not both */
			if (qdata->xcdev->dir_init == 1 ||
			    qdata->xcdev->dir_init == 2) {
					qdma_cdev_destroy(qdata->xcdev);
			} else { /* both bits are set so remove one */
				qdata->xcdev->dir_init >>= 1;
			}
		}
		memset(qdata, 0, sizeof(*qdata));
	}
	spin_unlock(&xpdev->cdev_lock);
}

static void remove_one(struct pci_dev *pdev)
{
	struct xlnx_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	if (!xpdev) {
		pr_info("%s NOT attached.\n", dev_name(&pdev->dev));
		return;
	}

	pr_info("%s pdev 0x%p, xdev 0x%p, hndl 0x%lx, qdma%d.\n",
		dev_name(&pdev->dev), pdev, xpdev, xpdev->dev_hndl, xpdev->idx);

	sysfs_remove_group(&pdev->dev.kobj, &pci_device_attr_group);

	qdma_cdev_device_cleanup(&xpdev->cdev_cb);

	xpdev_device_cleanup(xpdev);

	qdma_device_close(pdev, xpdev->dev_hndl);

	xpdev_free(xpdev);

	dev_set_drvdata(&pdev->dev, NULL);
}

#if defined(CONFIG_PCI_IOV) && !defined(__QDMA_VF__)
static int sriov_config(struct pci_dev *pdev, int num_vfs)
{
	struct xlnx_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	if (!xpdev) {
		pr_info("%s NOT attached.\n", dev_name(&pdev->dev));
		return -EINVAL;
	}

	pr_debug("%s pdev 0x%p, xdev 0x%p, hndl 0x%lx, qdma%d.\n",
		dev_name(&pdev->dev), pdev, xpdev, xpdev->dev_hndl, xpdev->idx);

	if (num_vfs > QDMA_VF_MAX) {
		pr_info("%s, clamp down # of VFs %d -> %d.\n",
			dev_name(&pdev->dev), num_vfs, QDMA_VF_MAX);
		num_vfs = QDMA_VF_MAX;
	}

	return qdma_device_sriov_config(pdev, xpdev->dev_hndl, num_vfs);
}
#endif


static pci_ers_result_t qdma_error_detected(struct pci_dev *pdev,
					pci_channel_state_t state)
{
	struct xlnx_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	switch (state) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		pr_warn("dev 0x%p,0x%p, frozen state error, reset controller\n",
			pdev, xpdev);
		pci_disable_device(pdev);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		pr_warn("dev 0x%p,0x%p, failure state error, req. disconnect\n",
			pdev, xpdev);
		return PCI_ERS_RESULT_DISCONNECT;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t qdma_slot_reset(struct pci_dev *pdev)
{
	struct xlnx_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	if (!xpdev) {
		pr_info("%s NOT attached.\n", dev_name(&pdev->dev));
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pr_info("0x%p restart after slot reset\n", xpdev);
	if (pci_enable_device_mem(pdev)) {
		pr_info("0x%p failed to renable after slot reset\n", xpdev);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(pdev);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	return PCI_ERS_RESULT_RECOVERED;
}

static void qdma_error_resume(struct pci_dev *pdev)
{
	struct xlnx_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	if (!xpdev) {
		pr_info("%s NOT attached.\n", dev_name(&pdev->dev));
		return;
	}

	pr_info("dev 0x%p,0x%p.\n", pdev, xpdev);
	pci_cleanup_aer_uncorrect_error_status(pdev);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
static void qdma_reset_prepare(struct pci_dev *pdev)
{
	struct xlnx_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	pr_info("%s pdev 0x%p, xdev 0x%p, hndl 0x%lx, qdma%d.\n",
		dev_name(&pdev->dev), pdev, xpdev, xpdev->dev_hndl, xpdev->idx);

	qdma_device_offline(pdev, xpdev->dev_hndl);
	qdma_device_flr_quirk_set(pdev, xpdev->dev_hndl);
	xpdev_queue_delete_all(xpdev);
	qdma_device_flr_quirk_check(pdev, xpdev->dev_hndl);
}

static void qdma_reset_done(struct pci_dev *pdev)
{
	struct xlnx_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	pr_info("%s pdev 0x%p, xdev 0x%p, hndl 0x%lx, qdma%d.\n",
		dev_name(&pdev->dev), pdev, xpdev, xpdev->dev_hndl, xpdev->idx);
	qdma_device_online(pdev, xpdev->dev_hndl);
}

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)
static void qdma_reset_notify(struct pci_dev *pdev, bool prepare)
{
	struct xlnx_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	pr_info("%s prepare %d, pdev 0x%p, xdev 0x%p, hndl 0x%lx, qdma%d.\n",
		dev_name(&pdev->dev), prepare, pdev, xpdev, xpdev->dev_hndl,
		xpdev->idx);

	if (prepare) {
		qdma_device_offline(pdev, xpdev->dev_hndl);
		qdma_device_flr_quirk_set(pdev, xpdev->dev_hndl);
		xpdev_queue_delete_all(xpdev);
		qdma_device_flr_quirk_check(pdev, xpdev->dev_hndl);
	} else
		qdma_device_online(pdev, xpdev->dev_hndl);
}
#endif
static const struct pci_error_handlers qdma_err_handler = {
	.error_detected	= qdma_error_detected,
	.slot_reset	= qdma_slot_reset,
	.resume		= qdma_error_resume,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
	.reset_prepare  = qdma_reset_prepare,
	.reset_done     = qdma_reset_done,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)
	.reset_notify   = qdma_reset_notify,
#endif
};

static struct pci_driver pci_driver = {
	.name = DRV_MODULE_NAME,
	.id_table = pci_ids,
	.probe = probe_one,
	.remove = remove_one,
//	.shutdown = shutdown_one,
#if defined(CONFIG_PCI_IOV) && !defined(__QDMA_VF__)
	.sriov_configure = sriov_config,
#endif
	.err_handler = &qdma_err_handler,
};

int xlnx_nl_init(void);
void xlnx_nl_exit(void);

static int __init qdma_mod_init(void)
{
	int rv;

	pr_info("%s", version);

	rv = libqdma_init();
	if (rv < 0)
		return rv;

	rv = xlnx_nl_init();
	if (rv < 0)
		return rv;

	rv = qdma_cdev_init();
	if (rv < 0)
		return rv;

	return pci_register_driver(&pci_driver);
}

static void __exit qdma_mod_exit(void)
{
	/* unregister this driver from the PCI bus driver */
	pci_unregister_driver(&pci_driver);

	xlnx_nl_exit();

	qdma_cdev_cleanup();

	libqdma_exit();
}

module_init(qdma_mod_init);
module_exit(qdma_mod_exit);
