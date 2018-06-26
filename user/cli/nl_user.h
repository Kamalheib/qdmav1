#ifndef __NL_USER_H__
#define __NL_USER_H__

#include <stdint.h>
#ifdef ERR_DEBUG
#include "qdma_nl.h"
#endif

struct xcmd_info;

enum q_parm_type {
	QPARM_IDX,
	QPARM_MODE,
	QPARM_DIR,
	QPARM_DESC,
	QPARM_WRB,
	QPARM_WRBSZ,
	QPARM_RNGSZ_IDX,
	QPARM_C2H_BUFSZ_IDX,
	QPARM_WRB_TMR_IDX,
	QPARM_WRB_CNTR_IDX,
	QPARM_WRB_TRIG_MODE,
#ifdef ERR_DEBUG
	QPARAM_ERR_NO,
#endif

	QPARM_MAX,
};

struct xcmd_q_parm {
	unsigned int sflags;
	uint32_t flags;
	uint32_t idx;
	uint32_t num_q;
	uint32_t range_start;
	uint32_t range_end;
	unsigned char wrb_entry_size;
	unsigned char qrngsz_idx;
	unsigned char c2h_bufsz_idx;
	unsigned char wrb_tmr_idx;
	unsigned char wrb_cntr_idx;
	unsigned char wrb_trig_mode;
	unsigned char is_qp;
#ifdef ERR_DEBUG
	unsigned int err_sel[2];
	unsigned int err_mask[2];
#endif
};

struct xcmd_intr {
	unsigned int vector;
	int start_idx;
	int end_idx;
};

struct xnl_cb {
	int fd;
	unsigned short family;
	unsigned int snd_seq;
	unsigned int rcv_seq;
};

int xnl_connect(struct xnl_cb *cb, int vf);
void xnl_close(struct xnl_cb *cb);
int xnl_send_cmd(struct xnl_cb *cb, struct xcmd_info *xcmd);

#endif /* ifndef __NL_USER_H__ */
