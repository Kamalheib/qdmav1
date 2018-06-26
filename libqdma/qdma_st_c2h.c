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

#define pr_fmt(fmt)     KBUILD_MODNAME ":%s: " fmt, __func__

#include "qdma_descq.h"

#include <linux/kernel.h>
#include <linux/delay.h>

#include "qdma_device.h"
#include "qdma_intr.h"
#include "qdma_regs.h"
#include "qdma_thread.h"
#include "qdma_context.h"
#include "thread.h"
#include "version.h"
#include "qdma_compat.h"

struct cmpl_info {
	/* parsed from cmpl entry */
	union {
		u8 fbits;
		struct cmpl_flag {
			u8 format:1;
			u8 color:1;
			u8 err:1;
			u8 desc_used:1;
			u8 filler:4;
		} f;
	};
	u8 rsvd;
	u16 len;
	/* for tracking */
	unsigned int pidx;
	__be64 *entry;
};

/*
 * ST C2H descq (i.e., freelist) RX buffers
 */

static inline void flq_unmap_one(struct qdma_sw_sg *sdesc,
				struct qdma_c2h_desc *desc, struct device *dev,
				unsigned char pg_order)
{
	if (sdesc->dma_addr) {
		desc->dst_addr = 0UL;
		dma_unmap_page(dev, sdesc->dma_addr, PAGE_SIZE << pg_order,
				DMA_FROM_DEVICE);
		sdesc->dma_addr = 0UL;
	}
}

static inline void flq_free_one(struct qdma_sw_sg *sdesc,
				struct qdma_c2h_desc *desc, struct device *dev,
				unsigned char pg_order)
{
	if (sdesc && sdesc->pg) {
		flq_unmap_one(sdesc, desc, dev, pg_order);
		__free_pages(sdesc->pg, pg_order);
		sdesc->pg = NULL;
	}
}

static inline int flq_fill_one(struct qdma_sw_sg *sdesc,
				struct qdma_c2h_desc *desc, struct device *dev,
				int node, unsigned char pg_order, gfp_t gfp)
{
        struct page *pg;
        dma_addr_t mapping;

	pg = alloc_pages_node(node, __GFP_COMP | gfp, pg_order);
	if (unlikely(!pg)) {
		pr_info("OOM, order %d.\n", pg_order);
		return -ENOMEM;
	}

	mapping = dma_map_page(dev, pg, 0, PAGE_SIZE << pg_order,
				PCI_DMA_FROMDEVICE);
	if (unlikely(dma_mapping_error(dev, mapping))) {
		dev_err(dev, "page 0x%p mapping error 0x%llx.\n",
			pg, (unsigned long long)mapping);
		__free_pages(pg, pg_order);
		return -EINVAL;
	}

	sdesc->pg = pg;	
	sdesc->dma_addr = mapping;
	sdesc->len = PAGE_SIZE << pg_order;
	sdesc->offset = 0;

	desc->dst_addr = sdesc->dma_addr;
	return 0;
}

void descq_flq_free_resource(struct qdma_descq *descq)
{
	struct xlnx_dma_dev *xdev = descq->xdev;
	struct device *dev = &xdev->conf.pdev->dev;
	struct qdma_flq *flq = &descq->flq;
	struct qdma_sw_sg *sdesc = flq->sdesc;
	struct qdma_c2h_desc *desc = flq->desc;
	unsigned char pg_order = flq->pg_order;
	int i;

	for (i = 0; i < flq->size; i++, sdesc++, desc++) {
		if (sdesc)
			flq_free_one(sdesc, desc, dev, pg_order);
		else
			break;
	}

	if (flq->sdesc) {
		kfree(flq->sdesc);
		flq->sdesc = NULL;
		flq->sdesc_info = NULL;
	}
	memset(flq, 0, sizeof(struct qdma_flq));
}

int descq_flq_alloc_resource(struct qdma_descq *descq)
{
	struct xlnx_dma_dev *xdev = descq->xdev;
	struct qdma_flq *flq = &descq->flq;
	struct device *dev = &xdev->conf.pdev->dev;
	int node = dev_to_node(dev);
	struct qdma_sw_sg *sdesc, *prev = NULL;
	struct qdma_sdesc_info *sinfo, *sprev = NULL;
	struct qdma_c2h_desc *desc = flq->desc;
	int i;
	int rv = 0;

	sdesc = kzalloc_node(flq->size * (sizeof(struct qdma_sw_sg) +
					  sizeof(struct qdma_sdesc_info)),
				GFP_KERNEL, node);
	if (!sdesc) {
		pr_info("OOM, sz %u.\n", flq->size);
		return -ENOMEM;
	}
	flq->sdesc = sdesc;
	flq->sdesc_info = sinfo = (struct qdma_sdesc_info *)(sdesc + flq->size);

	/* make the flq to be a linked list ring */
	for (i = 0; i < flq->size; i++, prev = sdesc, sdesc++,
					sprev = sinfo, sinfo++) {
		if (prev)
			prev->next = sdesc;
		if (sprev)
			sprev->next = sinfo;
	}	
	/* last entry's next points to the first entry */
	prev->next = flq->sdesc;
	sprev->next = flq->sdesc_info;

	for (sdesc = flq->sdesc, i = 0; i < flq->size; i++, sdesc++, desc++) {
//pr_err("flq sdesc 0x%p fill.\n", sdesc);
		rv = flq_fill_one(sdesc, desc, dev, node, flq->pg_order,
				GFP_KERNEL);
		if (rv < 0) {
			descq_flq_free_resource(descq);
			return rv;
		}
	}

	descq->cidx_wrb_pend = 0;
return 0;
}

static int qdma_flq_refill(struct qdma_descq *descq, int idx, int count,
			int recycle, gfp_t gfp)
{
	struct xlnx_dma_dev *xdev = descq->xdev;
	struct qdma_flq *flq = &descq->flq;
	struct qdma_sw_sg *sdesc = flq->sdesc + idx;
	struct qdma_c2h_desc *desc = flq->desc + idx;
	struct qdma_sdesc_info *sinfo = flq->sdesc_info + idx;
	int order = flq->pg_order;
	int i;

	for (i = 0; i < count; i++, idx++, sdesc++, desc++, sinfo++) {

		if (idx == flq->size) {
			idx = 0;
			sdesc = flq->sdesc;
			desc = flq->desc;
			sinfo = flq->sdesc_info;
		}

		if (recycle) {
			sdesc->len = PAGE_SIZE << order;
			sdesc->offset = 0;
		} else {
			struct device *dev = &xdev->conf.pdev->dev;
			int node = dev_to_node(dev);
			int rv;

			flq_unmap_one(sdesc, desc, dev, order);
			rv = flq_fill_one(sdesc, desc, dev, node, order, gfp);
			if (unlikely(rv < 0)) {
				if (rv == -ENOMEM)
					flq->alloc_fail++;
				else
					flq->mapping_err++;

				break;
			}
		}
		sinfo->fbits = 0;
		descq->avail++;
	}

	return i;
}

/*
 * 
 */
int descq_st_c2h_read(struct qdma_descq *descq, struct qdma_request *req,
			bool update_pidx, bool refill)
{
	struct qdma_sgt_req_cb *cb = qdma_req_cb_get(req);
	struct qdma_flq *flq = &descq->flq;
	unsigned int pidx = flq->pidx_pend;
	struct qdma_sw_sg *fsg = flq->sdesc + pidx;
	struct qdma_sw_sg *tsg = req->sgl;
	unsigned int fsgcnt = ring_idx_delta(descq->pidx, pidx, flq->size);
	unsigned int tsgoff = cb->sg_offset;
	unsigned int foff = 0;
	int i = 0, j = 0;
	unsigned int copied = 0;

	if (!fsgcnt)
		return 0;

	if (cb->sg_idx) {	
		for ( ; tsg && j < cb->sg_idx; j++) {
			tsg = tsg->next;
		}
		if (!tsg) {
			pr_err("tsg error, index %u/%u.\n",
				cb->sg_idx, req->sgcnt);
			return -EINVAL;
		}
	}

	while ((i < fsgcnt) && tsg) {
		unsigned int flen = fsg->len;
		unsigned char *faddr = page_address(fsg->pg);

		foff = 0;

		while(flen && tsg) {
			unsigned int toff = tsg->offset + tsgoff;
			unsigned int copy = min_t(unsigned int, flen,
						 tsg->len - tsgoff);

			memcpy(page_address(tsg->pg) + toff, faddr, copy);

			faddr += copy;
			flen -= copy;
			foff += copy;
			tsgoff += copy;
			copied += copy;

			if (tsgoff == tsg->len) {
				tsg = tsg->next;
				tsgoff = 0;
				j++;
			}
		}

		if (foff == fsg->len) {
			struct qdma_sdesc_info *sinfo = flq->sdesc_info + pidx;

 			if (sinfo->f.eop)
				descq->cidx_wrb_pend = sinfo->cidx;

			pidx = ring_idx_incr(pidx, 1, descq->conf.rngsz);

			i++;
			foff = 0;
			fsg = fsg->next;
		}
	}

	if (refill && i)
		qdma_flq_refill(descq, flq->pidx_pend, i, 1, GFP_ATOMIC);

	flq->pidx_pend = ring_idx_incr(flq->pidx_pend, i, flq->size);
	if (foff) {
		fsg->offset += foff;
		fsg->len -= foff;
	}

	if (update_pidx) {
		i = ring_idx_decr(flq->pidx_pend, 1, flq->size);
		descq_c2h_pidx_update(descq, i);
		descq_wrb_cidx_update(descq, descq->cidx_wrb_pend);
	}
	
	cb->sg_idx = j;
	cb->sg_offset = tsgoff;
	cb->left -= copied;

	flq->pkt_dlen -= copied;

	return copied;
}

static int qdma_c2h_packets_proc_dflt(struct qdma_descq *descq)
{
	struct qdma_sgt_req_cb *cb, *tmp;

	/* save udd */

	list_for_each_entry_safe(cb, tmp, &descq->pend_list, list) {
		int rv;

		/* check for zero length dma */
		if (!cb->left) {
			pr_info ("%s, cb 0x%p pending, zero len.\n",
				descq->conf.name, cb);

			qdma_sgt_req_done(descq, cb, 0);
			return 0;
		}

		rv = descq_st_c2h_read(descq, (struct qdma_request *)cb, 0, 0);	
		if (rv < 0) {
			pr_info("req 0x%p, error %d.\n", cb, rv);
			qdma_sgt_req_done(descq, cb, rv);
		}

		if (!cb->left) 
			qdma_sgt_req_done(descq, cb, 0);
		else
			break;
	}

	return 0;
}

static inline void wrb_next(struct qdma_descq *descq)
{
	u8 *desc_wrb_cur = descq->desc_wrb_cur + descq->wb_entry_len;

	descq->desc_wrb_cur = desc_wrb_cur;
	if (unlikely(++descq->cidx_wrb == descq->conf.rngsz_wrb)) {
		descq->cidx_wrb = 0;
		descq->color ^= 1;
		descq->desc_wrb_cur = descq->desc_wrb;
	}
}

static inline bool is_new_cmpl_entry(struct qdma_descq *descq,
					struct cmpl_info *cmpl)
{
	return cmpl->f.color == descq->color;
}

static int parse_cmpl_entry(struct qdma_descq *descq, struct cmpl_info *cmpl)
{
	__be64 *wrb = (__be64 *)descq->desc_wrb_cur;

	dma_rmb();

#if 0
	print_hex_dump(KERN_INFO, "cmpl entry ", DUMP_PREFIX_OFFSET,
			16, 1, (void *)wrb, descq->wb_entry_len,
			false);
#endif

	cmpl->entry = wrb;
	cmpl->f.format = wrb[0] & F_C2H_WB_ENTRY_F_FORMAT ? 1 : 0;
	cmpl->f.color = wrb[0] & F_C2H_WB_ENTRY_F_COLOR ? 1 : 0;
	cmpl->f.err = wrb[0] & F_C2H_WB_ENTRY_F_ERR ? 1 : 0;
	cmpl->f.desc_used = wrb[0] & F_C2H_WB_ENTRY_F_DESC_USED ? 1 : 0;
	if (!cmpl->f.format && cmpl->f.desc_used) {
		cmpl->len = (wrb[0] >> S_C2H_WB_ENTRY_LENGTH) &
				M_C2H_WB_ENTRY_LENGTH;
		/* zero length transfer allowed */
	} else
		cmpl->len = 0;

	if (unlikely(cmpl->f.err)) {
		pr_warn("%s, ERR compl entry %u error set\n",
				descq->conf.name, descq->cidx_wrb);
		goto err_out;
	}

	/* 
	 * format = 1 does not have length field, so the driver cannot
	 * figure out how many descriptor is used
	 */
	if (unlikely(cmpl->f.format)) {
		pr_err("%s: ERR cmpl. entry %u format=1.\n",
			descq->conf.name, descq->cidx_wrb);
		goto err_out;
	}

	if (unlikely(!cmpl->f.desc_used && !descq->conf.cmpl_udd_en)) {
		pr_warn("%s, ERR cmpl entry %u, desc_used 0, udd_en 0.\n",
			descq->conf.name, descq->cidx_wrb);
		goto err_out;
	}

	return 0;

err_out:
	descq->err = 1;
	print_hex_dump(KERN_INFO, "cmpl entry: ", DUMP_PREFIX_OFFSET,
			16, 1, (void *)wrb, descq->wb_entry_len,
			false);
        return -EINVAL;
}

static int rcv_pkt(struct qdma_descq *descq, struct cmpl_info *cmpl,
			unsigned int len)
{
	unsigned int pidx = cmpl->pidx;
	struct qdma_flq *flq = &descq->flq;
	unsigned int pg_shift = flq->pg_shift;
	unsigned int pg_mask = (1 << pg_shift) - 1;
	unsigned int rngsz = descq->conf.rngsz;
	/* zero length still uses one descriptor */
	int fl_nr = len ? (len + pg_mask) >> pg_shift : 1;
	unsigned int last = ring_idx_incr(cmpl->pidx, fl_nr - 1, rngsz);
	struct qdma_sw_sg *sdesc = flq->sdesc + last;

	descq->avail -= fl_nr;

	if (len) {
		unsigned int last_len = len & pg_mask;

		if (last_len)
			sdesc->len = last_len;
	} else {
		sdesc->len = 0;
	}

	if (descq->conf.fp_descq_c2h_packet) {
		int rv = descq->conf.fp_descq_c2h_packet(descq->conf.qidx,
				descq->conf.quld, len, fl_nr, flq->sdesc + pidx,
				descq->conf.cmpl_udd_en ?
				(unsigned char *)cmpl->entry : NULL);

		if (rv < 0)
			return rv;

		descq->cidx_wrb_pend = descq->cidx_wrb;
		flq->pidx_pend = last;
	} else {
		int i;
		struct qdma_sdesc_info *sinfo = flq->sdesc_info + pidx;
		unsigned int cidx_next = ring_idx_incr(descq->cidx_wrb, 1,
					descq->conf.rngsz_wrb);

		for (i = 0; i < fl_nr; i++, sinfo = sinfo->next) {
			WARN_ON(sinfo->f.valid);
			sinfo->f.valid = 1;
			sinfo->cidx = cidx_next;
		}

		flq->sdesc_info[pidx].f.sop = 1;
		flq->sdesc_info[last].f.eop = 1;

		flq->pkt_dlen += len;
		if (descq->conf.cmpl_udd_en)
			flq->udd_cnt++;
	}
	cmpl->pidx = ring_idx_incr(last, 1, rngsz);

	return 0;
}

static int rcv_udd_only(struct qdma_descq *descq, struct cmpl_info *cmpl)
{
#ifdef XMP_DISABLE_ST_C2H_CMPL
	__be64 wb_entry = cmpl->entry[0];
#endif
	struct qdma_flq *flq = &descq->flq;

	pr_info("%s, rcv udd.\n", descq->conf.name);
	print_hex_dump(KERN_INFO, "cmpl entry: ", DUMP_PREFIX_OFFSET,
			16, 1, (void *)cmpl->entry, descq->wb_entry_len,
			false);

	/* udd only: no descriptor used */
	if (descq->conf.fp_descq_c2h_packet)
		return descq->conf.fp_descq_c2h_packet(descq->conf.qidx,
				descq->conf.quld, 0, 0, NULL,
				(unsigned char *)cmpl->entry);
#ifdef XMP_DISABLE_ST_C2H_CMPL
	if ((wb_entry & (1 << 20)) > 0) {
		__be16 pkt_cnt = (wb_entry >> 32) & 0xFFFF;
		__be16 pkt_len = (wb_entry >> 48) & 0xFFFF;	
		int i;

		pr_info("pkt %u * %u.\n", pkt_len, pkt_cnt);
		for (i = 0; i < pkt_cnt; i++) {
			int rv = rcv_pkt(descq, cmpl, pkt_len);

			if (rv < 0)
				break;
		}
	}
#endif
	flq->udd_cnt++;

	return 0;
}

int descq_process_completion_st_c2h(struct qdma_descq *descq, int budget)
{
	struct qdma_c2h_wrb_wb *wb = (struct qdma_c2h_wrb_wb *)
				descq->desc_wrb_wb;
	unsigned int rngsz_wrb = descq->conf.rngsz_wrb;
	unsigned int pidx = descq->pidx;
	unsigned int cidx_wrb = descq->cidx_wrb;
	unsigned int pidx_wrb = descq->pidx_wrb;
	struct qdma_flq *flq = &descq->flq;
	unsigned int pidx_pend = flq->pidx_pend;
	bool uld_handler = descq->conf.fp_descq_c2h_packet ? true : false;
	int pend;
	int proc_cnt = 0;

	/* once an error happens, stop processing of the Q */
	if (descq->err) {
		pr_info("%s: err.\n", descq->conf.name);
		return 0;
	}

	/* if no uld user and no pending request */
	if (!uld_handler && list_empty(&descq->pend_list) &&
		list_empty(&descq->work_list))
		return 0;

	dma_rmb();

	pidx_wrb = wb->pidx;

	pend = ring_idx_delta(pidx_wrb, cidx_wrb, rngsz_wrb);
	if (!pend)
		return 0;

#if 0
	print_hex_dump(KERN_INFO, "cmpl status: ", DUMP_PREFIX_OFFSET,
				16, 1, (void *)wb, sizeof(*wb),
				false);
	pr_info("cmpl status: pidx 0x%x, cidx %x, color %d, int_state 0x%x.\n",
		wb->pidx, wb->cidx, wb->color_isr_status & 0x1,
		(wb->color_isr_status >> 1) & 0x3);
#endif

	if (!budget || budget > pend)
		budget = pend;

	while (likely(proc_cnt < budget)) {
		struct cmpl_info cmpl;
		int rv = parse_cmpl_entry(descq, &cmpl);

		/* completion entry error, q is halted */
		if (rv < 0)
			return rv;

		if (!is_new_cmpl_entry(descq, &cmpl))
			break;

		cmpl.pidx = pidx;

		if (cmpl.f.desc_used) {
			rv = rcv_pkt(descq, &cmpl, cmpl.len);
			if (rv < 0) /* cannot process now, stop */
				break;
		} else if (descq->conf.cmpl_udd_en) {
			/* udd only: no descriptor used */
			rv = rcv_udd_only(descq, &cmpl);
			if (rv < 0) /* cannot process now, stop */
				break;
		}

		if (rv < 0)
			break;

		pidx = cmpl.pidx;

		wrb_next(descq);
		proc_cnt++;
	}

	if (proc_cnt) {
		descq->pidx_wrb = pidx_wrb;
		descq->pidx = pidx;

		if (!descq->conf.fp_descq_c2h_packet)
			qdma_c2h_packets_proc_dflt(descq);

		/* some descq entries have been consumed */
		if (flq->pidx_pend != pidx_pend) {
			unsigned int cnt = ring_idx_delta(flq->pidx_pend,
						pidx_pend, flq->size);

			qdma_flq_refill(descq, pidx_pend, cnt,
					uld_handler ? 0 : 1, GFP_ATOMIC);

			cnt = ring_idx_decr(flq->pidx_pend, 1, flq->size);
			descq_c2h_pidx_update(descq, cnt);
		}

		descq_wrb_cidx_update(descq, descq->cidx_wrb_pend);
	}

	return 0;
}

int qdma_queue_c2h_peek(unsigned long dev_hndl, unsigned long id,
			unsigned int *udd_cnt, unsigned int *pkt_cnt,
			unsigned int *data_len)
{
        struct qdma_descq *descq = qdma_device_get_descq_by_id(
                                        (struct xlnx_dma_dev *)dev_hndl,
                                        id, NULL, 0, 1);
	struct qdma_flq *flq;

        if (!descq)
                return QDMA_ERR_INVALID_QIDX;

	flq = &descq->flq;
	*udd_cnt = flq->udd_cnt;	
	*pkt_cnt = flq->pkt_cnt;	
	*data_len = flq->pkt_dlen;	

	return 0;
}

int qdma_queue_packet_read(unsigned long dev_hndl, unsigned long id,
			struct qdma_request *req, struct qdma_cmpl_ctrl *cctrl)
{
	struct qdma_descq *descq = qdma_device_get_descq_by_id(
					(struct xlnx_dma_dev *)dev_hndl,
					id, NULL, 0, 1);
	struct qdma_sgt_req_cb *cb = qdma_req_cb_get(req);

	if (!descq)
		return QDMA_ERR_INVALID_QIDX;

	if (!descq->conf.st || !descq->conf.c2h) {
		pr_info("%s: st %d, c2h %d.\n",
			descq->conf.name, descq->conf.st, descq->conf.c2h);
		return -EINVAL;
	}

	memset(cb, 0, QDMA_REQ_OPAQUE_SIZE);
	init_waitqueue_head(&cb->wq);

	lock_descq(descq);
        descq_st_c2h_read(descq, req, 1, 1);
        unlock_descq(descq);

	return req->count - cb->left;
}
