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

#ifndef __LIBQDMA_CONFIG_H__
#define __LIBQDMA_CONFIG_H__

#include <linux/types.h>

/* QDMA IP absolute maximum */
#define QDMA_PF_MAX		4	/* 4 PFs */
#define QDMA_VF_MAX		252

/* current driver limit */
#define QDMA_Q_PER_PF_MAX	512
#define QDMA_Q_PER_VF_MAX	8
#define QDMA_INTR_COAL_RING_SIZE INTR_RING_SZ_4KB /* ring size is 4KB, i.e 512 entries */

/** Maximum data vectors to be used for each PF
  * TODO: Please note that for 2018.2 only one vector would be used
  * per pf and only one ring would be created for this vector
  * It is also assumed that all PF have the same number of data vectors
  * and currently different number of vectors per PF is not supported
  */

#define QDMA_DATA_VEC_PER_PF_MAX  1

/** Handler to set the qmax configuration value*/
int qdma_set_qmax(unsigned long dev_hndl, u32 qsets_max);

/** Handler to get the qmax configuration value*/
unsigned int qdma_get_qmax(unsigned long dev_hndl);

/** Handler to set the intr_rngsz configuration value*/
int qdma_set_intr_rngsz(unsigned long dev_hndl, u32 rngsz);

/** Handler to get the intr_rngsz configuration value*/
unsigned int qdma_get_intr_rngsz(unsigned long dev_hndl);

#ifndef __QDMA_VF__
/** Handler to set the wrb_acc configuration value*/
int qdma_set_wrb_acc(unsigned long dev_hndl, u32 wrb_acc);

/** Handler to get the wrb_acc configuration value*/
unsigned int qdma_get_wrb_acc(unsigned long dev_hndl);
#endif

#endif
