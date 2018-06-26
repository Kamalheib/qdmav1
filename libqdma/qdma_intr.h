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

#ifndef LIBQDMA_QDMA_INTR_H_
#define LIBQDMA_QDMA_INTR_H_

#include <linux/types.h>
#include <linux/workqueue.h>

struct xlnx_dma_dev;

/*
 * Interrupt ring data
 */
struct qdma_intr_ring {
	__be64 pidx:16;
	__be64 cidx:16;
	__be64 s_color:1;
	__be64 intr_satus:2;
	__be64 error:4;
	__be64 rsvd:11;
	__be64 error_int:1;
	__be64 intr_type:1;
	__be64 qid:11;
	__be64 coal_color:1;
};

void intr_teardown(struct xlnx_dma_dev *xdev);
int intr_setup(struct xlnx_dma_dev *xdev);
void intr_ring_teardown(struct xlnx_dma_dev *xdev);
int intr_context_setup(struct xlnx_dma_dev *xdev);
int intr_ring_setup(struct xlnx_dma_dev *xdev);
void intr_work(struct work_struct *work);

void qdma_err_intr_setup(struct xlnx_dma_dev *xdev, u8 rearm);
void qdma_enable_hw_err(struct xlnx_dma_dev *xdev, u8 hw_err_type);
int get_intr_ring_index(struct xlnx_dma_dev *xdev, u32 vector_index);

#ifdef ERR_DEBUG
void err_stat_handler(struct xlnx_dma_dev *xdev);
#endif

#endif /* LIBQDMA_QDMA_DEVICE_H_ */

