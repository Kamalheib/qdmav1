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

#ifndef QDMA_NL_H__
#define QDMA_NL_H__

#define XNL_NAME_PF		"xnl_pf"	/* no more than 15 characters */
#define XNL_NAME_VF		"xnl_vf"
#define XNL_VERSION		0x1

#define XNL_RESP_BUFLEN_MIN	 256
#define XNL_RESP_BUFLEN_MAX	 2048
#define XNL_ERR_BUFLEN		 64

#define XNL_STR_LEN_MAX		 20
/*
 * Q parameters
 */
#define XNL_QIDX_INVALID	0xFFFF

#define XNL_F_QMODE_ST	        0x00000001
#define XNL_F_QMODE_MM	        0x00000002
#define XNL_F_QDIR_H2C	        0x00000004
#define XNL_F_QDIR_C2H	        0x00000008
#define XNL_F_QDIR_BOTH         (XNL_F_QDIR_H2C | XNL_F_QDIR_C2H)
#define XNL_F_PFETCH_EN         0x00000010
#define XNL_F_DESC_BYPASS_EN	0x00000020
#define XNL_F_FETCH_CREDIT      0x00000040
#define XNL_F_WBK_ACC_EN        0x00000080
#define XNL_F_WBK_EN            0x00000100
#define XNL_F_WBK_PEND_CHK      0x00000200
#define XNL_F_WRB_STAT_DESC_EN  0x00000400
#define XNL_F_C2H_CMPL_INTR_EN  0x00000800
#define XNL_F_CMPL_UDD_EN       0x00001000

#define MAX_QFLAGS 13

#define QDMA_MAX_INT_RING_ENTRIES 512

/*
 * attributes (variables):
 * the index in this enum is used as a reference for the type,
 * userspace application has to indicate the corresponding type
 * the policy is used for security considerations
 */
enum xnl_attr_t {
	XNL_ATTR_GENMSG,
	XNL_ATTR_DRV_INFO,

	XNL_ATTR_DEV_IDX,	/* qdmaN */
	XNL_ATTR_PCI_BUS,
	XNL_ATTR_PCI_DEV,
	XNL_ATTR_PCI_FUNC,

	XNL_ATTR_DEV_CFG_BAR,
	XNL_ATTR_DEV_USR_BAR,
	XNL_ATTR_DEV_QSET_MAX,

	XNL_ATTR_REG_BAR_NUM,
	XNL_ATTR_REG_ADDR,
	XNL_ATTR_REG_VAL,

	XNL_ATTR_QIDX,
	XNL_ATTR_NUM_Q,
	XNL_ATTR_QFLAG,

	XNL_ATTR_WRB_DESC_SIZE,
	XNL_ATTR_QRNGSZ_IDX,
	XNL_ATTR_C2H_BUFSZ_IDX,
	XNL_ATTR_WRB_TIMER_IDX,
	XNL_ATTR_WRB_CNTR_IDX,
	XNL_ATTR_WRB_TRIG_MODE,

	XNL_ATTR_RANGE_START,
	XNL_ATTR_RANGE_END,

	XNL_ATTR_INTR_VECTOR_IDX,
	XNL_ATTR_INTR_VECTOR_START_IDX,
	XNL_ATTR_INTR_VECTOR_END_IDX,
	XNL_ATTR_RSP_BUF_LEN,
#ifdef ERR_DEBUG
	XNL_ATTR_QPARAM_ERR_SEL1,
	XNL_ATTR_QPARAM_ERR_SEL2,
	XNL_ATTR_QPARAM_ERR_MASK1,
	XNL_ATTR_QPARAM_ERR_MASK2,
#endif
	XNL_ATTR_MAX,
};

typedef enum {
	XNL_ST_C2H_WRB_DESC_SIZE_8B,
	XNL_ST_C2H_WRB_DESC_SIZE_16B,
	XNL_ST_C2H_WRB_DESC_SIZE_32B,
	XNL_ST_C2H_WRB_DESC_SIZE_RSVD
} xnl_st_c2h_wrb_desc_size;

static const char *xnl_attr_str[XNL_ATTR_MAX] = {
	"GENMSG",	/* XNL_ATTR_GENMSG */
	"DRV_INFO",	/* XNL_ATTR_DRV_INFO */

	"DEV_IDX",	/* XNL_ATTR_DEV_IDX */

	"DEV_PCIBUS",	/* XNL_ATTR_PCI_BUS */
	"DEV_PCIDEV",	/* XNL_ATTR_PCI_DEV */
	"DEV_PCIFUNC",	/* XNL_ATTR_PCI_FUNC */

	"DEV_CFG_BAR",	/* XNL_ATTR_DEV_CFG_BAR */
	"DEV_USR_BAR",	/* XNL_ATTR_DEV_USER_BAR */
	"DEV_QSETMAX",	/* XNL_ATTR_DEV_QSET_MAX */

	"REG_BAR",	/* XNL_ATTR_REG_BAR_NUM */
	"REG_ADDR",	/* XNL_ATTR_REG_ADDR */
	"REG_VAL",	/* XNL_ATTR_REG_VAL */

	"QIDX",		/* XNL_ATTR_QIDX */
	"NUM_Q",	/* XNL_ATTR_NUM_Q */
	"QFLAG",	/* XNL_ATTR_QFLAG */

	"WRB_DESC_SZ",	/* XNL_ATTR_WRB_DESC_SIZE */
	"QRINGSZ_IDX",	/* XNL_ATTR_QRNGSZ */
	"C2H_BUFSZ_IDX",	/* XNL_ATTR_QBUFSZ */
	"WRB_TIMER_IDX",	/* XNL_ATTR_WRB_TIMER_IDX */
	"WRB_CNTR_IDX",	/* XNL_ATTR_WRB_CNTR_IDX */
	"WRB_TRIG_MODE",	/* XNL_ATTR_WRB_TRIG_MODE */

	"RANGE_START",	/* XNL_ATTR_RANGE_START */
	"RANGE_END",	/* XNL_ATTR_RANGE_END */
	"INTR_VECTOR_IDX", /* XNL_ATTR_INTR_VECTOR_IDX */
	"INTR_VECTOR_START_IDX", /*XNL_ATTR_INTR_VECTOR_START_IDX */
	"INTR_VECTOR_END_IDX", /*XNL_ATTR_INTR_VECTOR_END_IDX */
	"RSP_BUF_LEN", /* XNL_ATTR_RSP_BUF_LEN */
#ifdef ERR_DEBUG
	"QPARAM_ERR_SEL1",
	"QPARAM_ERR_SEL2",
	"QPARAM_ERR_MASK1",
	"QPARAM_ERR_MASK2",
#endif

};

/* commands, 0 ~ 0x7F */
enum xnl_op_t {
	XNL_CMD_DEV_LIST,
	XNL_CMD_DEV_INFO,

	XNL_CMD_REG_DUMP,
	XNL_CMD_REG_RD,
	XNL_CMD_REG_WRT,

	XNL_CMD_Q_LIST,
	XNL_CMD_Q_ADD,
	XNL_CMD_Q_START,
	XNL_CMD_Q_STOP,
	XNL_CMD_Q_DEL,
	XNL_CMD_Q_DUMP,
	XNL_CMD_Q_DESC,
	XNL_CMD_Q_WRB,
	XNL_CMD_Q_RX_PKT,
#ifdef ERR_DEBUG
	XNL_CMD_Q_ERR_INDUCE,
#endif

	XNL_CMD_INTR_RING_DUMP,
	XNL_CMD_MAX,
};

static const char *xnl_op_str[XNL_CMD_MAX] = {
	"DEV_LIST",	/* XNL_CMD_DEV_LIST */
	"DEV_INFO",	/* XNL_CMD_DEV_INFO */

	"REG_DUMP",	/* XNL_CMD_REG_DUMP */
	"REG_READ",	/* XNL_CMD_REG_RD */
	"REG_WRITE",	/* XNL_CMD_REG_WRT */

	"Q_LIST",	/* XNL_CMD_Q_LIST */
	"Q_ADD",	/* XNL_CMD_Q_ADD */
	"Q_START",	/* XNL_CMD_Q_START */
	"Q_STOP",	/* XNL_CMD_Q_STOP */
	"Q_DEL",	/* XNL_CMD_Q_DEL */
	"Q_DUMP",	/* XNL_CMD_Q_DUMP */
	"Q_DESC",	/* XNL_CMD_Q_DESC */
	"Q_WRB",	/* XNL_CMD_Q_WRB */
	"Q_RX_PKT",	/* XNL_CMD_Q_RX_PKT */

	"INTR_RING_DUMP", /* XNL_CMD_INTR_RING_DUMP */
#ifdef ERR_DEBUG
	"Q_ERR_INDUCE"  /* XNL_CMD_Q_ERR_INDUCE */
#endif
};

/* Induce error - err index */
enum qdma_err_idx {
	err_ram_sbe,
	err_ram_dbe,
	err_dsc,
	err_trq,
	err_h2c_mm_0,
	err_h2c_mm_1,
	err_c2h_mm_0,
	err_c2h_mm_1,
	err_c2h_st,
	ind_ctxt_cmd_err,
	err_bdg,
	err_h2c_st,
	poison,
	ur_ca,
	param,
	addr,
	tag,
	flr,
	timeout,
	dat_poison,
	flr_cancel,
	dma,
	dsc,
	rq_cancel,
	dbe,
	sbe,
	unmapped,
	qid_range,
	vf_access_err,
	tcp_timeout,
	mty_mismatch,
	len_mismatch,
	qid_mismatch,
	desc_rsp_err,
	eng_wpl_data_par_err,
	msi_int_fail,
	err_desc_cnt,
	portid_ctxt_mismatch,
	portid_byp_in_mismatch,
	wrb_inv_q_err,
	wrb_qfull_err,
	fatal_mty_mismatch,
	fatal_len_mismatch,
	fatal_qid_mismatch,
	timer_fifo_ram_rdbe,
	fatal_eng_wpl_data_par_err,
	pfch_II_ram_rdbe,
	wb_ctxt_ram_rdbe,
	pfch_ctxt_ram_rdbe,
	desc_req_fifo_ram_rdbe,
	int_ctxt_ram_rdbe,
	int_qid2vec_ram_rdbe,
	wrb_coal_data_ram_rdbe,
	tuser_fifo_ram_rdbe,
	qid_fifo_ram_rdbe,
	payload_fifo_ram_rdbe,
	wpl_data_par_err,
	no_dma_dsc_err,
	wbi_mop_err,
	zero_len_desc_err,
	qdma_errs
};

#endif /* ifndef QDMA_NL_H__ */
