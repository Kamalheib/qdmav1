#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd_parse.h"
#include "nl_user.h"
#include "version.h"

static int read_range(int argc, char *argv[], int i, unsigned int *v1,
			unsigned int *v2);

static const char *progname;

#define Q_ADD_ATTR_IGNORE_MASK ~((1 << QPARM_IDX)  | \
				(1 << QPARM_MODE)  | \
				(1 << QPARM_DIR))
#define Q_START_ATTR_IGNORE_MASK ((1 << QPARM_MODE)  | \
                                  (1 << QPARM_DESC) | \
                                  (1 << QPARM_WRB))
#define Q_STOP_ATTR_IGNORE_MASK ~((1 << QPARM_IDX) | \
				(1 << QPARM_DIR))
#define Q_DEL_ATTR_IGNORE_MASK ~((1 << QPARM_IDX)  | \
				(1 << QPARM_DIR))
#define Q_DUMP_ATTR_IGNORE_MASK ~((1 << QPARM_IDX)  | \
				(1 << QPARM_DIR) | \
				(1 << QPARM_DESC) | \
				(1 << QPARM_WRB))
#define Q_DUMP_PKT_ATTR_IGNORE_MASK ~((1 << QPARM_IDX)  | \
					(1 << QPARM_DIR))
#define Q_H2C_ATTR_IGNORE_MASK ((1 << QPARM_C2H_BUFSZ_IDX) | \
				(1 << QPARM_WRB_TMR_IDX) | \
				(1 << QPARM_WRB_CNTR_IDX) | \
				(1 << QPARM_WRB_TRIG_MODE) | \
				(1 << QPARM_WRBSZ))

#define Q_ADD_FLAG_IGNORE_MASK ~(XNL_F_QMODE_ST | \
				XNL_F_QMODE_MM | \
				XNL_F_QDIR_BOTH)
#define Q_START_FLAG_IGNORE_MASK (XNL_F_QMODE_ST | \
                                  XNL_F_QMODE_MM)
#define Q_STOP_FLAG_IGNORE_MASK ~(XNL_F_QMODE_ST | \
					XNL_F_QMODE_MM | \
					XNL_F_QDIR_BOTH)
#define Q_DEL_FLAG_IGNORE_MASK  ~(XNL_F_QMODE_ST | \
					XNL_F_QMODE_MM | \
					XNL_F_QDIR_BOTH)
#define Q_DUMP_FLAG_IGNORE_MASK  ~(XNL_F_QMODE_ST | \
					XNL_F_QMODE_MM | \
					XNL_F_QDIR_BOTH)
#define Q_DUMP_PKT_FLAG_IGNORE_MASK ~(XNL_F_QMODE_ST | \
					XNL_F_QMODE_MM | \
					XNL_F_QDIR_C2H)
#define Q_H2C_FLAG_IGNORE_MASK  (XNL_F_C2H_CMPL_INTR_EN | \
				XNL_F_CMPL_UDD_EN)

#ifdef ERR_DEBUG
char *qdma_err_str[qdma_errs] = {
	"err_ram_sbe",
	"err_ram_dbe",
	"err_dsc",
	"err_trq",
	"err_h2c_mm_0",
	"err_h2c_mm_1",
	"err_c2h_mm_0",
	"err_c2h_mm_1",
	"err_c2h_st",
	"ind_ctxt_cmd_err",
	"err_bdg",
	"err_h2c_st",
	"poison",
	"ur_ca",
	"param",
	"addr",
	"tag",
	"flr",
	"timeout",
	"dat_poison",
	"dsc",
	"dma",
	"rq_cancel",
	"dbe",
	"sbe",
	"unmapped",
	"qid_range",
	"vf_access_err",
	"tcp_timeout",
	"mty_mismatch",
	"len_mismatch",
	"qid_mismatch",
	"desc_rsp_err",
	"eng_wpl_data_par_err",
	"msi_int_fail",
	"err_desc_cnt",
	"portid_ctxt_mismatch",
	"portid_byp_in_mismatch",
	"wrb_inv_q_err",
	"wrb_qfull_err",
	"mty_mismatch",
	"len_mismatch",
	"qid_mismatch",
	"timer_fifo_ram_rdbe",
	"eng_wpl_data_par_err",
	"pfch_II_ram_rdbe",
	"wb_ctxt_ram_rdbe",
	"pfch_ctxt_ram_rdbe",
	"desc_req_fifo_ram_rdbe",
	"int_ctxt_ram_rdbe",
	"int_qid2vec_ram_rdbe",
	"wrb_coal_data_ram_rdbe",
	"tuser_fifo_ram_rdbe",
	"qid_fifo_ram_rdbe",
	"payload_fifo_ram_rdbe",
	"wpl_data_par_err",
	"no_dma_dsc_err",
	"wbi_mop_err",
	"zero_len_desc_err"
};
#endif

/*
 * qset add h2c mm h2c_id
 */
static void __attribute__((noreturn)) usage(FILE *fp)
{
	fprintf(fp, "Usage: %s [dev|qdma[vf]<N>] [operation] \n", progname);
	fprintf(fp, "\tdev [operation]: system wide FPGA operations\n");
	fprintf(fp, 
		"\t\tlist                             list all qdma functions\n");
	fprintf(fp,
		"\tqdma[N] [operation]: per QDMA FPGA operations\n");
	fprintf(fp,
		"\t\tq list                           list all queues\n"
		"\t\tq add idx <N> [mode <mm|st>] [dir <h2c|c2h|bi>] - add a queue\n"
		"\t\t                                    *mode default to mm\n"
		"\t\t                                    *dir default to h2c\n"
	        "\t\tq add list <start_idx> <num_Qs> [mode <mm|st>] [dir <h2c|c2h|bi>] - add multiple queues at once\n"
	        "\t\tq start idx <N> [dir <h2c|c2h|bi>] [idx_ringsz <0:15>] [idx_bufsz <0:15>] [idx_tmr <0:15>]\n"
		"                                    [idx_cntr <0:15>] [trigmode <every|usr_cnt|usr|usr_tmr|dis>] [wrbsz <0|1|2|3>]\n"
	        "                                    [bypass_en] [pfetch_en] [dis_wbk] [dis_wbk_acc] [dis_wbk_pend_chk] [c2h_udd_en]\n"
	        "                                    [dis_fetch_credit] [dis_wrb_stat] [c2h_cmpl_intr_en] - start a single queue\n"
	        "\t\tq start list <start_idx> <num_Qs> [dir <h2c|c2h|bi>] [idx_bufsz <0:15>] [idx_tmr <0:15>]\n"
		"                                    [idx_cntr <0:15>] [trigmode <every|user|cnt|tmr|dis>] [wrbsz <0|1|2|3>]\n"
	        "                                    [bypass_en] [pfetch_en] [dis_wbk] [dis_wbk_acc] [dis_wbk_pend_chk]\n"
	        "                                    [dis_fetch_credit] [dis_wrb_stat] [c2h_cmpl_intr_en] - start multiple queues at once\n"
	        "\t\tq stop idx <N> dir [<h2c|c2h|bi>] - stop a single queue\n"
	        "\t\tq stop list <start_idx> <num_Qs> dir [<h2c|c2h|bi>] - stop list of queues at once\n"
	        "\t\tq del idx <N> dir [<h2c|c2h|bi>] - delete a queue\n"
	        "\t\tq del list <start_idx> <num_Qs> dir [<h2c|c2h|bi>] - delete list of queues at once\n"
		"\t\tq dump idx <N> dir [<h2c|c2h|bi>]   dump queue param\n"
		"\t\tq dump list <start_idx> <num_Qs> dir [<h2c|c2h|bi>]   dump queue param\n"
		"\t\tq dump idx <N> dir [<h2c|c2h|bi>] desc <x> <y>\n"
		"\t\t                                 dump desc ring entry x ~ y\n"
		"\t\tq dump list <start_idx> <num_Qs> dir [<h2c|c2h|bi>] desc <x> <y>\n"
		"\t\t                                 dump desc ring entry x ~ y\n"
		"\t\tq dump idx <N> dir [<h2c|c2h|bi>] wrb <x> <y>\n"
		"\t\t                                 dump wrb ring entry x ~ y\n"
		"\t\tq dump list <start_idx> <num_Qs> dir [<h2c|c2h|bi>] wrb <x> <y>\n"
		"\t\t                                 dump wrb ring entry x ~ y\n"
		"\t\tq dump pkt idx <N> [dir c2h]\n"
		"\t\t                                 read and dump the next packet received, ST C2H only\n"
#ifdef ERR_DEBUG
		"\t\tq err help - help to induce errors  \n"
		"\t\tq err idx <N> <num_errs> [list of <err <[1|0]>>] dir <[h2c|c2h|bi]> - induce errors on q idx <N>  \n"
#endif
		);
	fprintf(fp,
		"\t\treg dump [dmap <Q> <N>]          register dump. Only dump dmap registers if dmap is specified.\n"
		"                                     specify dmap range to dump: Q=queue, N=num of queues\n"
		"\t\treg read [bar <N>] <addr>        read a register\n"
		"\t\treg write [bar <N>] <addr> <val> write a register\n");
	fprintf(fp,
		"\t\tintring dump vector <N> <start_idx> <end_idx>      interrupt ring dump for vector number <N>  \n"
		"\t\t                                                   for intrrupt entries :<start_idx> --- <end_idx>\n");

	exit(fp == stderr ? 1 : 0);
}

static int arg_read_int(char *s, uint32_t *v)
{
    char *p;

    *v = strtoul(s, &p, 0);
    if (*p) {
        warnx("bad parameter \"%s\", integer expected", s);
        return -EINVAL;
    }
    return 0;
}

static int parse_ifname(char *name, struct xcmd_info *xcmd)
{
	int rv;
	int len = strlen(name);
	int pos, i;
	uint32_t v;

	/* qdmaN of qdmavfN*/
	if (len >= 10) {
		warnx("interface name %s too long, expect qdma<N>.\n", name);
		return -EINVAL;
	}
	if (strncmp(name, "qdma", 4)) {
		warnx("bad interface name %s, expect qdma<N>.\n", name);
		return -EINVAL;
	}
	if (name[4] == 'v' && name[5] == 'f') {
		xcmd->vf = 1;
		pos = 6;
	} else {
		xcmd->vf = 0;
		pos = 4;
	}
	for (i = pos; i < len; i++) {
		if (!isdigit(name[i])) {
			warnx("%s unexpected <qdmaN>, %d.\n", name, i);
			return -EINVAL;
		}
	}

	rv = arg_read_int(name + pos, &v);
	if (rv < 0)
		return rv;

	xcmd->if_idx = v;
	return 0;
}

#define get_next_arg(argc, argv, i) \
	if (++(*i) >= argc) { \
		warnx("%s missing parameter after \"%s\".\n", __FUNCTION__, argv[--(*i)]); \
		return -EINVAL; \
	}

#define __get_next_arg(argc, argv, i) \
	if (++i >= argc) { \
		warnx("%s missing parameter aft \"%s\".\n", __FUNCTION__, argv[--i]); \
		return -EINVAL; \
	}

static int next_arg_read_int(int argc, char *argv[], int *i, unsigned int *v)
{
	get_next_arg(argc, argv, i);
	return arg_read_int(argv[*i], v);
}

static int next_arg_read_pair(int argc, char *argv[], int *start, char *s,
			unsigned int *v, int optional)
{
	int rv;
	int i = *start;

	/* string followed by an int */
	if ((i + 2) >= argc) {
		if (optional) {
			warnx("No optional parm %s after \"%s\".\n", s, argv[i]);
			return 0;
		} else {
			warnx("missing parameter after \"%s\".\n", argv[i]);
			return -EINVAL;
		}
	}

	__get_next_arg(argc, argv, i);

	if (!strcmp(s, argv[i])) {
		get_next_arg(argc, argv, &i);
		*start = i;
		return arg_read_int(argv[i], v);
	}

	warnx("bad parameter, \"%s\".\n", argv[i]);
	return -EINVAL;
}

static int validate_regcmd(enum xnl_op_t qcmd, struct xcmd_reg	*regcmd)
{
	int invalid = 0;

	switch(qcmd) {
		case XNL_CMD_REG_DUMP:
			break;
		case XNL_CMD_REG_RD:
		case XNL_CMD_REG_WRT:
			if ((regcmd->bar != 0) && (regcmd->bar != 2)) {
				printf("dmactl: bar %d number out of range\n",
				       regcmd->bar);
				invalid = -EINVAL;
				break;
			}
			if ((regcmd->bar == 0) &&
				!is_valid_addr(regcmd->bar, regcmd->reg)) {
				printf("dmactl: Invalid address 0x%x in bar %u\n",
				       regcmd->reg, regcmd->bar);
				invalid = -EINVAL;
			}
			break;
		default:
			invalid = -EINVAL;
			break;
	}

	return invalid;
}

static int parse_reg_cmd(int argc, char *argv[], int i, struct xcmd_info *xcmd)
{
	struct xcmd_reg	*regcmd = &xcmd->u.reg;
	int rv;
	int args_valid;

	/*
	 * reg dump
	 * reg read [bar <N>] <addr> 
	 * reg write [bar <N>] <addr> <val> 
	 */

	memset(regcmd, 0, sizeof(struct xcmd_reg));
	if (!strcmp(argv[i], "dump")) {
		xcmd->op = XNL_CMD_REG_DUMP;
		i++;

		if (i < argc) {
			if (!strcmp(argv[i], "dmap")) {
				get_next_arg(argc, argv, &i);
				rv = read_range(argc, argv, i, &regcmd->range_start,
					&regcmd->range_end);
				if (rv < 0)
					return rv;
				i = rv;
			}
		}

	} else if (!strcmp(argv[i], "read")) {
		xcmd->op = XNL_CMD_REG_RD;

		get_next_arg(argc, argv, &i);
		if (!strcmp(argv[i], "bar")) {
			rv = next_arg_read_int(argc, argv, &i, &regcmd->bar);
			if (rv < 0)
				return rv;
			regcmd->sflags |= XCMD_REG_F_BAR_SET;
			get_next_arg(argc, argv, &i);
		}
		rv = arg_read_int(argv[i], &regcmd->reg);
		if (rv < 0)
			return rv;
		regcmd->sflags |= XCMD_REG_F_REG_SET;

		i++;

	} else if (!strcmp(argv[i], "write")) {
		xcmd->op = XNL_CMD_REG_WRT;

		get_next_arg(argc, argv, &i);
		if (!strcmp(argv[i], "bar")) {
			rv = next_arg_read_int(argc, argv, &i, &regcmd->bar);
			if (rv < 0)
				return rv;
			regcmd->sflags |= XCMD_REG_F_BAR_SET;
			get_next_arg(argc, argv, &i);
		}
		rv = arg_read_int(argv[i], &xcmd->u.reg.reg);
		if (rv < 0)
			return rv;
		regcmd->sflags |= XCMD_REG_F_REG_SET;

		rv = next_arg_read_int(argc, argv, &i, &xcmd->u.reg.val);
		if (rv < 0)
			return rv;
		regcmd->sflags |= XCMD_REG_F_VAL_SET;

		i++;
	}

	args_valid = validate_regcmd(xcmd->op, regcmd);

	return (args_valid == 0) ? i : args_valid;
}

static int read_range(int argc, char *argv[], int i, unsigned int *v1,
			unsigned int *v2)
{
	int rv;

	/* range */
	rv = arg_read_int(argv[i], v1);
	if (rv < 0)
		return rv;

	get_next_arg(argc, argv, &i);
	rv = arg_read_int(argv[i], v2);
	if (rv < 0)
		return rv;

	if (v2 < v1) {
		warnx("invalid range %u ~ %u.\n", *v1, *v2);
		return -EINVAL;
	}

	return ++i;
}

static char *qparm_type_str[QPARM_MAX] = {
	"idx",
	"mode",
	"dir",
	"desc",
	"wrb",
	"wrbsz",
	"idx_ringsz",
	"idx_bufsz",
	"idx_tmr",
	"idx_cntr",
	"trigmode",
#ifdef ERR_DEBUG
	"err_no"
#endif
};

static char *qflag_type_str[MAX_QFLAGS] = {
	"mode",
	"mode",
	"dir",
	"dir",
	"pfetch_en",
	"fetch_credit",
	"dis_fetch_credit",
	"dis_wbk_acc",
	"dis_wbk",
	"dis_wbk_pend_chk",
	"dis_wrb_stat",
	"c2h_cmpl_intr_en",
	"c2h_udd_en"
};

#define IS_SIZE_IDX_VALID(x) (x < 16)

static void print_ignored_params(uint32_t ignore_mask, uint8_t isflag)
{
	unsigned int maxcount = isflag ? MAX_QFLAGS : QPARM_MAX;
	char **qparam = isflag ? qflag_type_str : qparm_type_str;
	unsigned int i;
	unsigned int count = 0;

	for (i = 0; ignore_mask && (i < MAX_QFLAGS); i++) {
		if (ignore_mask & 0x01)
			warnx("Warn: Ignoring attr: %s", qparam[count]);
		ignore_mask >>= 1;
		count++;
	}
}

static int validate_qcmd(enum xnl_op_t qcmd, struct xcmd_q_parm *qparm)
{
	int invalid = 0;
	switch(qcmd) {
		case XNL_CMD_Q_LIST:
			if (qparm->sflags)
				warnx("Warn: Ignoring all attributes and flags");
			break;
		case XNL_CMD_Q_ADD:
			print_ignored_params(qparm->sflags &
					     Q_ADD_ATTR_IGNORE_MASK, 0);
			print_ignored_params(qparm->flags &
					     Q_ADD_FLAG_IGNORE_MASK, 1);
			break;
		case XNL_CMD_Q_START:
			if (!IS_SIZE_IDX_VALID(qparm->c2h_bufsz_idx)) {
				warnx("dmactl: C2H Buf index out of range");
				invalid = -EINVAL;
			}
			if (!IS_SIZE_IDX_VALID(qparm->qrngsz_idx)) {
				warnx("dmactl: Queue ring size index out of range");
				invalid = -EINVAL;
			}
			if (!IS_SIZE_IDX_VALID(qparm->wrb_cntr_idx)) {
				warnx("dmactl: WRB counter index out of range");
				invalid = -EINVAL;
			}
			if (!IS_SIZE_IDX_VALID(qparm->wrb_tmr_idx)) {
				warnx("dmactl: WRB timer index out of range");
				invalid = -EINVAL;
			}
			if (qparm->wrb_entry_size >
				XNL_ST_C2H_WRB_DESC_SIZE_RSVD) {
				warnx("dmactl: WRB entry size out of range");
				invalid = -EINVAL;
			}
			if (qparm->flags & XNL_F_CMPL_UDD_EN) {
				if (!(qparm->sflags & (1 << QPARM_WRBSZ))) {
					printf("Error: wrbsz required for enabling c2h udd packet\n");
					invalid = -EINVAL;
					break;
				} else {
					if (qparm->wrb_entry_size ==
						XNL_ST_C2H_WRB_DESC_SIZE_RSVD) {
						printf("Error: valid wrbsz for c2h_udd_en is <1|2|3>\n");
						invalid = -EINVAL;
						break;
					}
				}
			}
			if (qparm->flags & XNL_F_QDIR_H2C) {
				print_ignored_params(qparm->sflags &
						     (Q_START_ATTR_IGNORE_MASK |
						     Q_H2C_ATTR_IGNORE_MASK), 0);
				print_ignored_params(qparm->flags &
						     (Q_START_FLAG_IGNORE_MASK |
						     Q_H2C_FLAG_IGNORE_MASK), 1);
			}

			break;
		case XNL_CMD_Q_STOP:
			print_ignored_params(qparm->sflags &
					     Q_STOP_ATTR_IGNORE_MASK, 0);
			print_ignored_params(qparm->flags &
					     Q_STOP_FLAG_IGNORE_MASK, 1);
			break;
		case XNL_CMD_Q_DEL:
			print_ignored_params(qparm->sflags &
					     Q_DEL_ATTR_IGNORE_MASK, 0);
			print_ignored_params(qparm->flags &
					     Q_DEL_FLAG_IGNORE_MASK, 1);
			break;
		case XNL_CMD_Q_WRB:
		case XNL_CMD_Q_DUMP:
			if ((qparm->sflags & ((1 << QPARM_DESC) |
					(1 << QPARM_WRB))) == ((1 << QPARM_DESC) |
							(1 << QPARM_WRB))) {
				invalid = -EINVAL;
				printf("Error: Both desc and wrb attr cannot be taken for Q DUMP\n");
				break;
			}
		case XNL_CMD_Q_DESC:
			print_ignored_params(qparm->sflags &
					     Q_DUMP_ATTR_IGNORE_MASK, 0);
			print_ignored_params(qparm->flags &
					     Q_DUMP_FLAG_IGNORE_MASK, 1);
			break;
		case XNL_CMD_Q_RX_PKT:
			if (qparm->flags & XNL_F_QDIR_H2C) {
				printf("Rx dump packet is st c2h only command\n");
				invalid = -EINVAL;
				break;
			}
			print_ignored_params(qparm->sflags &
					     Q_DUMP_PKT_ATTR_IGNORE_MASK, 0);
			print_ignored_params(qparm->flags &
					     Q_DUMP_PKT_FLAG_IGNORE_MASK, 1);
			break;
#ifdef ERR_DEBUG
		case XNL_CMD_Q_ERR_INDUCE:
			break;
#endif
		default:
			invalid = -EINVAL;
			break;
	}

	return invalid;
}

#ifdef ERR_DEBUG
static unsigned char get_err_num(char *err)
{
	uint32_t i;

	for (i = 0; i < qdma_errs; i++)
		if (!strcmp(err, qdma_err_str[i]))
			break;

	return i;
}
#endif

static int read_qparm(int argc, char *argv[], int i, struct xcmd_q_parm *qparm,
			unsigned int f_arg_required)
{
	int rv;
	uint32_t v1, v2;
	unsigned int f_arg_set = 0;;
	unsigned int mask;

	/*
	 * idx <val>
	 * list <start_idx> <num_q>
	 * ringsz <val>
	 * bufsz <val>
	 * mode <mm|st>
	 * dir <h2c|c2h|bi>
	 * cdev <0|1>
	 * bypass <0|1>
	 * desc <x> <y>
	 * wrb <x> <y>
	 * wrbsz <0|1|2|3>
	 */

	qparm->idx = XNL_QIDX_INVALID;

	while (i < argc) {
#ifdef ERR_DEBUG
		if ((f_arg_required & (1 << QPARAM_ERR_NO)) &&
				(!strcmp(argv[i], "help"))) {
			uint32_t i;

			fprintf(stdout, "q err idx <N> <num_errs> [list of <err <[1|0]>>] dir <[h2c|c2h|bi]>\n");
			fprintf(stdout, "Supported errors:\n");
			for (i = 0; i < qdma_errs; i++)
				fprintf(stdout, "\t%s\n", qdma_err_str[i]);

			return argc;
		}
#endif
		if (!strcmp(argv[i], "idx")) {
			rv = next_arg_read_int(argc, argv, &i, &v1);
			if (rv < 0)
				return rv;

			qparm->idx = v1;
			f_arg_set |= 1 << QPARM_IDX;
			qparm->num_q = 1;
#ifdef ERR_DEBUG
			if (f_arg_required & (1 << QPARAM_ERR_NO)) {
				unsigned int num_errs;
				unsigned int j;

				rv = next_arg_read_int(argc, argv, &i, &v1);
				if (rv < 0)
					return rv;

				num_errs = v1;
				if (num_errs > qdma_errs) {
					printf("Invalid number of errors. Should be less than %d\n",
					       qdma_errs);
					return -EINVAL;
				}

				for (j = 0; j < num_errs; j++) {
					unsigned char err_no;
					unsigned int enable;

					get_next_arg(argc, argv, (&i));

					err_no = get_err_num(argv[i]);
					if (err_no >= qdma_errs) {
						fprintf(stderr,
							"unknown err %s.\n",
							argv[i]);
						return -EINVAL;
					}

					rv = next_arg_read_int(argc, argv, &i, &v1);
					if (rv < 0)
						return rv;

					enable = v1;

					if (err_no > 31) {
						qparm->err_sel[1] |= enable ?
								(1 << (err_no - 31)) :
								0;
						qparm->err_mask[1] |=
								(1 << (err_no - 31));
					} else {
						qparm->err_sel[0] |= enable ?
								(1 << err_no) :
								0;
						qparm->err_mask[0] |=
								(1 << err_no);
					}
				}
				i++;
				f_arg_set |= 1 << QPARAM_ERR_NO;
			} else
				i++;
#else
			i++;
#endif

		} else if (!strcmp(argv[i], "list")) {
			rv = next_arg_read_int(argc, argv, &i, &v1);
			if (rv < 0)
				return rv;

			qparm->idx = v1;
			f_arg_set |= 1 << QPARM_IDX;
			rv = next_arg_read_int(argc, argv, &i, &v1);
			if (rv < 0)
				return rv;

			qparm->num_q = v1;
			i++;

		} else if (!strcmp(argv[i], "mode")) {
			get_next_arg(argc, argv, (&i));

			if (!strcmp(argv[i], "mm")) {
				qparm->flags |= XNL_F_QMODE_MM;
			} else if (!strcmp(argv[i], "st")) {
				qparm->flags |= XNL_F_QMODE_ST;
			} else {
				warnx("unknown q mode %s.\n", argv[i]);
				return -EINVAL;
			}
			f_arg_set |= 1 << QPARM_MODE;
			i++;

		} else if (!strcmp(argv[i], "dir")) {
			get_next_arg(argc, argv, (&i));

			if (!strcmp(argv[i], "h2c")) {
				qparm->flags |= XNL_F_QDIR_H2C;
			} else if (!strcmp(argv[i], "c2h")) {
				qparm->flags |= XNL_F_QDIR_C2H;
			} else if (!strcmp(argv[i], "bi")) {
				qparm->flags |= XNL_F_QDIR_BOTH;
			} else {
				warnx("unknown q dir %s.\n", argv[i]);
				return -EINVAL;
			}
			f_arg_set |= 1 << QPARM_DIR;
			i++;

		} else if (!strcmp(argv[i], "idx_bufsz")) {
			rv = next_arg_read_int(argc, argv, &i, &v1);
			if (rv < 0)
				return rv;

			qparm->c2h_bufsz_idx = v1;

			f_arg_set |= 1 << QPARM_C2H_BUFSZ_IDX;
			i++;
		
		} else if (!strcmp(argv[i], "idx_ringsz")) {
			rv = next_arg_read_int(argc, argv, &i, &v1);
			if (rv < 0)
				return rv;

			qparm->qrngsz_idx = v1;
			f_arg_set |= 1 << QPARM_RNGSZ_IDX;
			i++;
		} else if (!strcmp(argv[i], "idx_tmr")) {
			rv = next_arg_read_int(argc, argv, &i, &v1);
			if (rv < 0)
				return rv;

			qparm->wrb_tmr_idx = v1;
			f_arg_set |= 1 << QPARM_WRB_TMR_IDX;
			i++;
		} else if (!strcmp(argv[i], "idx_cntr")) {
			rv = next_arg_read_int(argc, argv, &i, &v1);
			if (rv < 0)
				return rv;

			qparm->wrb_cntr_idx = v1;
			f_arg_set |= 1 << QPARM_WRB_CNTR_IDX;
			i++;

		} else if (!strcmp(argv[i], "trigmode")) {
			get_next_arg(argc, argv, (&i));

			if (!strcmp(argv[i], "every")) {
				v1 = 1;
			} else if (!strcmp(argv[i], "usr_cnt")) {
				v1 = 2;
			} else if (!strcmp(argv[i], "usr")) {
				v1 = 3;
			} else if (!strcmp(argv[i], "usr_tmr")) {
				v1=4;
			} else if (!strcmp(argv[i], "dis")) {
				v1 = 0;
			} else {
				warnx("unknown q dir %s.\n", argv[i]);
				return -EINVAL;
			}

			qparm->wrb_trig_mode = v1;
			f_arg_set |= 1 << QPARM_WRB_TRIG_MODE;
			i++;
		} else if (!strcmp(argv[i], "desc")) {
			get_next_arg(argc, argv, &i);
			rv = read_range(argc, argv, i, &qparm->range_start,
					&qparm->range_end);
			if (rv < 0)
				return rv;
			i = rv;
			f_arg_set |= 1 << QPARM_DESC;

		} else if (!strcmp(argv[i], "wrb")) {
		    get_next_arg(argc, argv, &i);
		    rv = read_range(argc, argv, i, &qparm->range_start,
			    &qparm->range_end);
		    if (rv < 0)
			return rv;
		    i = rv;
		    f_arg_set |= 1 << QPARM_WRB;

		} else if (!strcmp(argv[i], "wrbsz")) {
		    get_next_arg(argc, argv, &i);
		    sscanf(argv[i], "%hhu", &qparm->wrb_entry_size);
		    f_arg_set |= 1 << QPARM_WRBSZ;
		    i++;
		} else if (!strcmp(argv[i], "pfetch_en")) {
			qparm->flags |= XNL_F_PFETCH_EN;
			i++;
		} else if (!strcmp(argv[i], "bypass_en")) {
			qparm->flags |= XNL_F_DESC_BYPASS_EN;
			i++;
		} else if (!strcmp(argv[i], "c2h_cmpl_intr_en")) {
			qparm->flags |= XNL_F_C2H_CMPL_INTR_EN;
			i++;
		} else if (!strcmp(argv[i], "dis_wbk")) {
			qparm->flags &= ~XNL_F_WBK_EN;
			i++;
		} else if (!strcmp(argv[i], "dis_wbk_acc")) {
			qparm->flags &= ~XNL_F_WBK_ACC_EN;
			i++;
		} else if (!strcmp(argv[i], "dis_wbk_pend_chk")) {
			qparm->flags &= ~XNL_F_WBK_PEND_CHK;
			i++;
		} else if (!strcmp(argv[i], "dis_fetch_credit")) {
			qparm->flags &= ~XNL_F_FETCH_CREDIT;
			i++;
		} else if (!strcmp(argv[i], "dis_wrb_stat")) {
			qparm->flags &= ~XNL_F_WRB_STAT_DESC_EN;
			i++;
		} else if (!strcmp(argv[i], "c2h_udd_en")) {
			qparm->flags |= XNL_F_CMPL_UDD_EN;
			i++;
		} else {
			warnx("unknown q parameter %s.\n", argv[i]);
			return -EINVAL;
		}
	}
	/* check for any missing mandatory parameters */
	mask = f_arg_set & f_arg_required;
	if (mask != f_arg_required) {
		int i;
		unsigned int bit_mask = 1;

		mask = (mask ^ f_arg_required) & f_arg_required;	

		for (i = 0; i < QPARM_MAX; i++, bit_mask <<= 1) {
			if (!(bit_mask & f_arg_required))
				continue;
			warnx("missing q parameter %s.\n", qparm_type_str[i]);
			return -EINVAL;
		}
	}

	if (!(f_arg_set & 1 << QPARM_DIR)) {
		/* default to H2C */
		warnx("Warn: Default dir set to \'h2c\'");
		f_arg_set |= 1 << QPARM_DIR;
		qparm->flags |=  XNL_F_QDIR_H2C;
	}

	qparm->sflags = f_arg_set;

	return argc;
}

static int parse_q_cmd(int argc, char *argv[], int i, struct xcmd_info *xcmd)
{
	struct xcmd_q_parm *qparm = &xcmd->u.qparm;
	int rv;
	int args_valid;

	/*
	 * q list
	 * q add idx <N> mode <mm|st> [dir <h2c|c2h|bi>] [cdev <0|1>] [wrbsz <0|1|2|3>]
	 * q start idx <N> dir <h2c|c2h|bi>
	 * q stop idx <N> dir <h2c|c2h|bi>
	 * q del idx <N> dir <h2c|c2h|bi>
	 * q dump idx <N> dir <h2c|c2h|bi>
	 * q dump idx <N> dir <h2c|c2h|bi> desc <x> <y>
	 * q dump idx <N> dir <h2c|c2h|bi> wrb <x> <y>
	 * q pkt idx <N>
	 */

	if (!strcmp(argv[i], "list")) {
		xcmd->op = XNL_CMD_Q_LIST;
		return ++i;
	} else if (!strcmp(argv[i], "add")) {
		unsigned int mask;

		xcmd->op = XNL_CMD_Q_ADD;
		get_next_arg(argc, argv, &i);
		rv = read_qparm(argc, argv, i, qparm, (1 << QPARM_IDX));
		/* error checking of parameter values */
		if (qparm->sflags & (1 << QPARM_MODE)) {
			mask = XNL_F_QMODE_MM | XNL_F_QMODE_ST;
			if ((qparm->flags & mask) == mask) {
				warnx("mode mm/st cannot be combined.\n");
				return -EINVAL;
			}
		} else {
			/* default to MM */
			warnx("Warn: Default mode set to \'mm\'");
			qparm->sflags |= 1 << QPARM_MODE;
			qparm->flags |=  XNL_F_QMODE_MM;
		}

	} else if (!strcmp(argv[i], "start")) {
		xcmd->op = XNL_CMD_Q_START;
		get_next_arg(argc, argv, &i);
		rv = read_qparm(argc, argv, i, qparm, (1 << QPARM_IDX));
		qparm->flags |= (XNL_F_WBK_EN | XNL_F_WBK_ACC_EN |
				XNL_F_WBK_PEND_CHK | XNL_F_WRB_STAT_DESC_EN |
				XNL_F_FETCH_CREDIT);
		if (qparm->flags & (XNL_F_QDIR_C2H | XNL_F_QMODE_ST) ==
				(XNL_F_QDIR_C2H | XNL_F_QMODE_ST)) {
			if (!(qparm->sflags & (1 << QPARM_WRBSZ))) {
					/* default to rsv */
					qparm->wrb_entry_size = 3;
					qparm->sflags |=
						(1 << QPARM_WRBSZ);
			}
		}
	} else if (!strcmp(argv[i], "stop")) {
		xcmd->op = XNL_CMD_Q_STOP;
		get_next_arg(argc, argv, &i);
		rv = read_qparm(argc, argv, i, qparm, (1 << QPARM_IDX));

	} else if (!strcmp(argv[i], "del")) {
		xcmd->op = XNL_CMD_Q_DEL;
		get_next_arg(argc, argv, &i);
		rv = read_qparm(argc, argv, i, qparm, (1 << QPARM_IDX));

	} else if (!strcmp(argv[i], "dump")) {
		xcmd->op = XNL_CMD_Q_DUMP;
		get_next_arg(argc, argv, &i);
		rv = read_qparm(argc, argv, i, qparm, (1 << QPARM_IDX));
	} else if (!strcmp(argv[i], "pkt")) {
		xcmd->op = XNL_CMD_Q_RX_PKT;
		get_next_arg(argc, argv, &i);
		rv = read_qparm(argc, argv, i, qparm, (1 << QPARM_IDX));
#ifdef ERR_DEBUG
	} else if (!strcmp(argv[i], "err")) {
		xcmd->op = XNL_CMD_Q_ERR_INDUCE;
		get_next_arg(argc, argv, &i);
		rv = read_qparm(argc, argv, i, qparm, ((1 << QPARM_IDX) |
		                (1 << QPARAM_ERR_NO)));
#endif
	}
	
	if (rv < 0)
		return rv;
	i = rv;

	if (xcmd->op == XNL_CMD_Q_DUMP) {
		unsigned int mask = (1 << QPARM_DESC) | (1 << QPARM_WRB);

		if ((qparm->sflags & mask) == mask) {
			warnx("dump wrb/desc cannot be combined.\n");
			return -EINVAL;
		}
		if ((qparm->sflags & (1 << QPARM_DESC)))
			xcmd->op = XNL_CMD_Q_DESC;
		else if ((qparm->sflags & (1 << QPARM_WRB)))
			xcmd->op = XNL_CMD_Q_WRB;
	}

	args_valid = validate_qcmd(xcmd->op, qparm);

	return (args_valid == 0) ? i : args_valid;

	return i;
}

static int parse_dev_cmd(int argc, char *argv[], int i, struct xcmd_info *xcmd)
{
	if (!strcmp(argv[i], "list")) {
		xcmd->op = XNL_CMD_DEV_LIST;
		i++;
	}
	return i;
}

static int parse_intr_cmd(int argc, char *argv[], int i, struct xcmd_info *xcmd)
{
	struct xcmd_intr	*intrcmd = &xcmd->u.intr;
	int rv;

	/*
	 * intr dump vector <N>
	 */

	memset(intrcmd, 0, sizeof(struct xcmd_intr));
	if (!strcmp(argv[i], "dump")) {
		xcmd->op = XNL_CMD_INTR_RING_DUMP;

		get_next_arg(argc, argv, &i);
		if (!strcmp(argv[i], "vector")) {
			rv = next_arg_read_int(argc, argv, &i, &intrcmd->vector);
			if (rv < 0)
				return rv;
		}
		rv = next_arg_read_int(argc, argv, &i, &intrcmd->start_idx);
		if (rv < 0) {
			intrcmd->start_idx = 0;
			intrcmd->end_idx = QDMA_MAX_INT_RING_ENTRIES - 1;
			goto func_ret;
		}
		rv = next_arg_read_int(argc, argv, &i, &intrcmd->end_idx);
		if (rv < 0)
			intrcmd->end_idx = QDMA_MAX_INT_RING_ENTRIES - 1;
	}
func_ret:
	i++;
	return i;
}

int parse_cmd(int argc, char *argv[], struct xcmd_info *xcmd)
{
	char *ifname;
	int i;
	int rv;

	memset(xcmd, 0, sizeof(struct xcmd_info));

	progname = argv[0];

	if (argc == 1) 
		usage(stderr);

	if (argc == 2) {
		if (!strcmp(argv[1], "?") || !strcmp(argv[1], "-h") ||
		    !strcmp(argv[1], "help") || !strcmp(argv[1], "--help"))
			usage(stdout);

		if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
			printf("%s version %s\n", PROGNAME, VERSION);
			printf("%s\n", COPYRIGHT);
			exit(0);
		}
	}

	if (!strcmp(argv[1], "dev")) {
		rv = parse_dev_cmd(argc, argv, 2, xcmd);
		goto done;
	}

	/* which dma fpga */
	ifname = argv[1];
	rv = parse_ifname(ifname, xcmd);
	if (rv < 0)
		return rv;

	if (argc == 2) {
		rv = 2;
		xcmd->op = XNL_CMD_DEV_INFO;
		goto done;
	}

	i = 3;
	if (!strcmp(argv[2], "reg")) {
		rv = parse_reg_cmd(argc, argv, i, xcmd);
	} else if (!strcmp(argv[2], "q")) {
		rv = parse_q_cmd(argc, argv, i, xcmd);
	} else if (!strcmp(argv[2], "intring")){
		rv = parse_intr_cmd(argc, argv, i, xcmd);
	} else {
		warnx("bad parameter \"%s\".\n", argv[2]);
		return -EINVAL;
	}

done:
	if (rv < 0)
		return rv;
	i = rv;
	
	if (i < argc) {
		warnx("unexpected parameter \"%s\".\n", argv[i]);
		return -EINVAL;
	}
	return 0;
}
