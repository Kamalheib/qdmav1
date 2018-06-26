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

/*
 * This file is used to allow the driver to be compiled under multiple
 * versions of Linux with as few obtrusive in-line #ifdef's as possible.
 */

#ifndef __QDMA_COMPAT_H
#define __QDMA_COMPAT_H

#include <linux/version.h>
#include <asm/barrier.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)

#ifndef dma_rmb
#define dma_rmb		rmb
#endif /* #ifndef dma_rmb */

#ifndef dma_wmb
#define dma_wmb		wmb
#endif /* #ifndef dma_wmb */

#endif

#endif /* #ifndef __QDMA_COMPAT_H */
