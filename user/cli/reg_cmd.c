#include <endian.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "reg_cmd.h"
#include "cmd_parse.h"

#define QDMA_CFG_BAR_SIZE 0xB400
#define QDMA_USR_BAR_SIZE 0x100

struct xdev_info {
	unsigned char bus;
	unsigned char dev;
	unsigned char func;
	unsigned char config_bar;
	unsigned char user_bar;
};

static struct xreg_info qdma_config_regs[] = {

	/* QDMA_TRQ_SEL_GLBL1 (0x00000) */
	{"CFG_BLOCK_ID",				0x00, 0, 0, 0, 0,},
	{"CFG_BUSDEV",					0x04, 0, 0, 0, 0,},
	{"CFG_PCIE_MAX_PL_SZ",				0x08, 0, 0, 0, 0,},
	{"CFG_PCIE_MAX_RDRQ_SZ",			0x0C, 0, 0, 0, 0,},
	{"CFG_SYS_ID",					0x10, 0, 0, 0, 0,},
	{"CFG_MSI_EN",					0x14, 0, 0, 0, 0,},
	{"CFG_PCIE_DATA_WIDTH",				0x18, 0, 0, 0, 0,},
	{"CFG_PCIE_CTRL",				0x1C, 0, 0, 0, 0,},
	{"CFG_AXI_USR_MAX_PL_SZ",			0x40, 0, 0, 0, 0,},
	{"CFG_AXI_USR_MAX_RDRQ_SZ",			0x44, 0, 0, 0, 0,},
	{"CFG_WR_FLUSH_TIMEOUT",			0x60, 0, 0, 0, 0,},

	/* QDMA_TRQ_SEL_GLBL2 (0x00100) */
	{"GLBL2_ID",					0x100, 0, 0, 0, 0,},
	{"GLBL2_PF_BL_INT",				0x104, 0, 0, 0, 0,},
	{"GLBL2_PF_VF_BL_INT",				0x108, 0, 0, 0, 0,},
	{"GLBL2_PF_BL_EXT",				0x10C, 0, 0, 0, 0,},
	{"GLBL2_PF_VF_BL_EXT",				0x110, 0, 0, 0, 0,},
	{"GLBL2_CHNL_INST",				0x114, 0, 0, 0, 0,},
	{"GLBL2_CHNL_QDMA",				0x118, 0, 0, 0, 0,},
	{"GLBL2_CHNL_STRM",				0x11C, 0, 0, 0, 0,},
	{"GLBL2_QDMA_CAP",				0x120, 0, 0, 0, 0,},
	{"GLBL2_PASID_CAP",				0x128, 0, 0, 0, 0,},
	{"GLBL2_FUNC_RET",				0x12C, 0, 0, 0, 0,},
	{"GLBL2_SYS_ID",				0x130, 0, 0, 0, 0,},
	{"GLBL2_MISC_CAP",				0x134, 0, 0, 0, 0,},
	{"GLBL2_DBG_PCIE_RQ",				0x1B8, 2, 0, 0, 0,},
	{"GLBL2_DBG_AXIMM_WR",				0x1C0, 2, 0, 0, 0,},
	{"GLBL2_DBG_AXIMM_RD",				0x1C8, 2, 0, 0, 0,},

	/* QDMA_TRQ_SEL_GLBL (0x00200) */
	{"GLBL_RNGSZ",					0x204, 16, 0, 0, 0,},
	{"GLBL_SCRATCH",				0x244, 0,  0, 0, 0,},
	{"GLBL_ERR_STAT",				0x248, 0,  0, 0, 0,},
	{"GLBL_ERR_MASK",				0x24C, 0,  0, 0, 0,},
	{"GLBL_DSC_CFG",				0x250, 0,  0, 0, 0,},
	{"GLBL_DSC_ERR_STS",				0x254, 0,  0, 0, 0,},
	{"GLBL_DSC_ERR_MSK",				0x258, 0,  0, 0, 0,},
	{"GLBL_DSC_ERR_LOG",				0x25C, 2,  0, 0, 0,},
	{"GLBL_TRQ_ERR_STS",				0x264, 0,  0, 0, 0,},
	{"GLBL_TRQ_ERR_MSK",				0x268, 0,  0, 0, 0,},
	{"GLBL_TRQ_ERR_LOG",				0x26C, 0,  0, 0, 0,},
	{"GLBL_DSC_DBG_DAT",				0x270, 2,  0, 0, 0,},
	{"GLBL_DSC_DBG_CTL",				0x278, 0,  0, 0, 0,},

	/* QDMA_TRQ_SEL_FMAP (0x00400) */
	/* TODO: max 256, display 4 for now */
	{"TRQ_SEL_FMAP",				0x400, 4, 0, 0, 0,},

	/* QDMA_TRQ_SEL_IND (0x00800) */
	{"IND_CTXT_DATA",				0x804, 4, 0, 0, 0,},
	{"IND_CTXT_MASK",				0x814, 4, 0, 0, 0,},
	{"IND_CTXT_CMD",				0x824, 0, 0, 0, 0,},
	{"IND_CAUSE",					0x828, 0, 0, 0, 0,},
	{"IND_ENABLE",					0x82C, 0, 0, 0, 0,},

	/* QDMA_TRQ_SEL_C2H (0x00A00) */
	{"C2H_TIMER_CNT",				0xA00, 16, 0, 0, 0,},
	{"C2H_CNT_THRESH",				0xA40, 16, 0, 0, 0,},
	{"C2H_QID2VEC_MAP_QID",				0xA80, 0, 0, 0, 0,},
	{"C2H_QID2VEC_MAP",				0xA84, 0, 0, 0, 0,},
	{"C2H_STAT_S_AXIS_C2H_ACCEPTED",		0xA88, 0, 0, 0, 0,},
	{"C2H_STAT_S_AXIS_WRB_ACCEPTED",		0xA8C, 0, 0, 0, 0,},
	{"C2H_STAT_DESC_RSP_PKT_ACCEPTED",		0xA90, 0, 0, 0, 0,},
	{"C2H_STAT_AXIS_PKG_CMP",			0xA94, 0, 0, 0, 0,},
	{"C2H_STAT_DESC_RSP_ACCEPTED",			0xA98, 0, 0, 0, 0,},
	{"C2H_STAT_DESC_RSP_CMP",			0xA9C, 0, 0, 0, 0,},
	{"C2H_STAT_WRQ_OUT",				0xAA0, 0, 0, 0, 0,},
	{"C2H_STAT_WPL_REN_ACCEPTED",			0xAA4, 0, 0, 0, 0,},
	{"C2H_STAT_TOTAL_WRQ_LEN",			0xAA8, 0, 0, 0, 0,},
	{"C2H_STAT_TOTAL_WPL_LEN",			0xAAC, 0, 0, 0, 0,},
	{"C2H_BUF_SZ",					0xAB0, 16, 0, 0, 0,},
	{"C2H_ERR_STAT",				0xAF0, 0, 0, 0, 0,},
	{"C2H_ERR_MASK",				0xAF4, 0, 0, 0, 0,},
	{"C2H_FATAL_ERR_STAT",				0xAF8, 0, 0, 0, 0,},
	{"C2H_FATAL_ERR_MASK",				0xAFC, 0, 0, 0, 0,},
	{"C2H_FATAL_ERR_ENABLE",			0xB00, 0, 0, 0, 0,},
	{"C2H_ERR_INT",					0xB04, 0, 0, 0, 0,},
	{"C2H_PFCH_CFG",				0xB08, 0, 0, 0, 0,},
	{"C2H_INT_TIMER_TICK",				0xB0C, 0, 0, 0, 0,},
	{"C2H_STAT_DESC_RSP_DROP_ACCEPTED",		0xB10, 0, 0, 0, 0,},
	{"C2H_STAT_DESC_RSP_ERR_ACCEPTED",		0xB14, 0, 0, 0, 0,},
	{"C2H_STAT_DESC_REQ",				0xB18, 0, 0, 0, 0,},
	{"C2H_STAT_DEBUG_DMA_ENG",			0xB1C, 4, 0, 0, 0,},
	{"C2H_INTR_MSIX",				0xB2C, 0, 0, 0, 0,},
	{"C2H_FIRST_ERR_QID",				0xB30, 0, 0, 0, 0,},
	{"STAT_NUM_WRB_IN",				0xB34, 0, 0, 0, 0,},
	{"STAT_NUM_WRB_OUT",				0xB38, 0, 0, 0, 0,},
	{"STAT_NUM_WRB_DRP",				0xB3C, 0, 0, 0, 0,},
	{"STAT_NUM_STAT_DESC_OUT",			0xB40, 0, 0, 0, 0,},
	{"STAT_NUM_DSC_CRDT_SENT",			0xB44, 0, 0, 0, 0,},
	{"STAT_NUM_FCH_DSC_RCVD",			0xB48, 0, 0, 0, 0,},
	{"STAT_NUM_BYP_DSC_RCVD",			0xB4C, 0, 0, 0, 0,},
	{"C2H_WRB_COAL_CFG",				0xB50, 0, 0, 0, 0,},
	{"C2H_INTR_H2C_REQ",				0xB54, 0, 0, 0, 0,},
	{"C2H_INTR_C2H_MM_REQ",				0xB58, 0, 0, 0, 0,},
	{"C2H_INTR_ERR_INT_REQ",			0xB5C, 0, 0, 0, 0,},
	{"C2H_INTR_C2H_ST_REQ",				0xB60, 0, 0, 0, 0,},
	{"C2H_INTR_H2C_ERR_MM_MSIX_ACK",		0xB64, 0, 0, 0, 0,},
	{"C2H_INTR_H2C_ERR_MM_MSIX_FAIL",		0xB68, 0, 0, 0, 0,},
	{"C2H_INTR_H2C_ERR_MM_NO_MSIX",			0xB6C, 0, 0, 0, 0,},
	{"C2H_INTR_H2C_ERR_MM_CTXT_INVAL",		0xB70, 0, 0, 0, 0,},
	{"C2H_INTR_C2H_ST_MSIX_ACK",			0xB74, 0, 0, 0, 0,},
	{"C2H_INTR_C2H_ST_MSIX_FAIL",			0xB78, 0, 0, 0, 0,},
	{"C2H_INTR_C2H_ST_NO_MSIX",			0xB7C, 0, 0, 0, 0,},
	{"C2H_INTR_C2H_ST_CTXT_INVAL",			0xB80, 0, 0, 0, 0,},
	{"C2H_STAT_WR_CMP",				0xB84, 0, 0, 0, 0,},
	{"C2H_STAT_DEBUG_DMA_ENG_4",			0xB88, 0, 0, 0, 0,},
	{"C2H_STAT_DEBUG_DMA_ENG_5",			0xB8C, 0, 0, 0, 0,},
	{"C2H_DBG_PFCH_QID",				0xB90, 0, 0, 0, 0,},
	{"C2H_DBG_PFCH",				0xB94, 0, 0, 0, 0,},
	{"C2H_INT_DEBUG",				0xB98, 0, 0, 0, 0,},
	{"C2H_STAT_IMM_ACCEPTED",			0xB9C, 0, 0, 0, 0,},
	{"C2H_STAT_MARKER_ACCEPTED",			0xBA0, 0, 0, 0, 0,},
	{"C2H_STAT_DISABLE_CMP_ACCEPTED",		0xBA4, 0, 0, 0, 0,},
	{"C2H_C2H_PAYLOAD_FIFO_CRDT_CNT",		0xBA8, 0, 0, 0, 0,},

	/* QDMA_TRQ_SEL_H2C(0x00C00) Register Space*/
	{"H2C_ERR_STAT",				0xE00, 0, 0, 0, 0,},
	{"H2C_ERR_MASK",				0xE04, 0, 0, 0, 0,},
	{"H2C_ERR_FIRST_QID",				0xE08, 0, 0, 0, 0,},
	{"H2C_DBG_REG",					0xE0C, 5, 0, 0, 0,},

	/* QDMA_TRQ_SEL_C2H_MM (0x1000) */
	{"C2H_MM0_CONTROL",				0x1004, 0, 0, 0, 0,},
	{"C2H_MM0_STATUS",				0x1040, 0, 0, 0, 0,},
	{"C2H_MM0_CMPL_DSC_CNT",			0x1048, 0, 0, 0, 0,},
	{"C2H_MM0_ERR_CODE_EN_MASK",			0x1054, 0, 0, 0, 0,},
	{"C2H_MM0_ERR_CODE",				0x1058, 0, 0, 0, 0,},
	{"C2H_MM0_ERR_INFO",				0x105C, 0, 0, 0, 0,},
	{"C2H_MM0_PERF_MON_CTRL",			0x10C0, 0, 0, 0, 0,},
	{"C2H_MM0_PERF_MON_CY_CNT",			0x10C4, 2, 0, 0, 0,},
	{"C2H_MM0_PERF_MON_DATA_CNT",			0x10CC, 2, 0, 0, 0,},
	{"C2H_MM_DBG_INFO",				0x10E8, 2, 0, 0, 0,},

	/* QDMA_TRQ_SEL_H2C_MM (0x1200)*/
	{"H2C_MM0_CONTROL",				0x1204, 0, 0, 0, 0,},
	{"H2C_MM0_STATUS",				0x1240, 0, 0, 0, 0,},
	{"H2C_MM0_CMPL_DSC_CNT",			0x1248, 0, 0, 0, 0,},
	{"H2C_MM0_ERR_CODE_EN_MASK",			0x1254, 0, 0, 0, 0,},
	{"H2C_MM0_ERR_CODE",				0x1258, 0, 0, 0, 0,},
	{"H2C_MM0_ERR_INFO",				0x125C, 0, 0, 0, 0,},
	{"H2C_MM0_PERF_MON_CTRL",			0x12C0, 0, 0, 0, 0,},
	{"H2C_MM0_PERF_MON_CY_CNT",			0x12C4, 2, 0, 0, 0,},
	{"H2C_MM0_PERF_MON_DATA_CNT",			0x12CC, 2, 0, 0, 0,},
	{"H2C_MM0_DBG_INFO",				0x12E8, 0, 0, 0, 0,},

	/* TODO only read registers below if MM1 engines are present */
	/*{"C2H_MM1_CONTROL",			0x1104, 0, 0, 0, 0,},
	{"C2H_MM1_STATUS",			0x1140, 0, 0, 0, 0,},
	{"C2H_MM1_CMPL_DSC_CNT",		0x1148, 0, 0, 0, 0,},
	{"H2C_MM1_CONTROL",			0x1304, 0, 0, 0, 0,},
	{"H2C_MM1_STATUS",			0x1340, 0, 0, 0, 0,},
	{"H2C_MM1_CMPL_DSC_CNT",		0x1348, 0, 0, 0, 0,},*/

	/* TODO QDMA_TRQ_MSIX (0x1400) */

	/* QDMA_PF_MAILBOX (0x2400) */
	{"FUNC_STATUS",					0x2400, 0, 0, 0, 0,},
	{"FUNC_CMD",					0x2404, 0, 0, 0, 0,},
	{"FUNC_INTR_VEC",				0x2408, 0, 0, 0, 0,},
	{"TARGET_FUNC",					0x240C, 0, 0, 0, 0,},
	{"INTR_CTRL",					0x2410, 0, 0, 0, 0,},
	{"PF_ACK",					0x2420, 8, 0, 0, 0,},
	{"FLR_CTRL_STATUS",				0x2500, 0, 0, 0, 0,},
	{"MSG_IN",					0x2C00, 32, 0, 0, 0,},
	{"MSG_OUT",					0x3000, 32, 0, 0, 0,},

	{"", 0, 0, 0 }
};

static struct xreg_info qdma_dmap_regs[] = {
/* QDMA_TRQ_SEL_QUEUE_PF (0x6400) */
	{"DMAP_SEL_INT_CIDX",				0x6400, 512, 0x10, 0, 0,},
	{"DMAP_SEL_H2C_DSC_PIDX",			0x6404, 512, 0x10, 0, 0,},
	{"DMAP_SEL_C2H_DSC_PIDX",			0x6408, 512, 0x10, 0, 0,},
	{"DMAP_SEL_WRB_CIDX",				0x640C, 512, 0x10, 0, 0,},
	{"", 0, 0, 0 }
};

/*
 * INTERNAL: for debug testing only
 */
static struct xreg_info qdma_user_regs[] = {
	{"ST_C2H_QID",					0x0, 0, 0, 0, 0,},
	{"ST_C2H_PKTLEN",				0x4, 0, 0, 0, 0,},
	{"ST_C2H_CONTROL",				0x8, 0, 0, 0, 0,},
	/*  ST_C2H_CONTROL:
	 *	[1] : start C2H
	 *	[2] : immediate data
	 *	[3] : every packet statrs with 00 instead of continuous data
	 *	      stream until # of packets is complete
	 *	[31]: gen_user_reset_n
	 */
	{"ST_H2C_CONTROL",				0xC, 0, 0, 0, 0,},
	/*  ST_H2C_CONTROL:
	 *	[0] : clear match for H2C transfer
	 */
	{"ST_H2C_STATUS",				0x10, 0, 0, 0, 0,},
	{"ST_H2C_XFER_CNT",				0x14, 0, 0, 0, 0,},
	{"ST_C2H_PKT_CNT",				0x20, 0, 0, 0, 0,},
	{"ST_C2H_WRB_DATA",				0x30, 8, 0, 0, 0,},
	{"ST_C2H_WRB_SIZE",				0x50, 0, 0, 0, 0,},
	{"ST_SCRATCH_REG",				0x60, 2, 0, 0, 0,},
	{"ST_C2H_PKT_DROP",				0x88, 0, 0, 0, 0,},
	{"ST_C2H_PKT_ACCEPT",				0x8C, 0, 0, 0, 0,},
	{"DSC_BYPASS_LOOP",				0x90, 0, 0, 0, 0,},
	{"USER_INTERRUPT",				0x94, 0, 0, 0, 0,},
	{"USER_INTERRUPT_MASK",				0x98, 0, 0, 0, 0,},
	{"USER_INTERRUPT_VEC",				0x9C, 0, 0, 0, 0,},
	{"DMA_CONTROL",					0xA0, 0, 0, 0, 0,},
	{"", 0, 0, 0 }
};


/*
 * Register I/O through mmap of BAR0.
 */

/* /sys/bus/pci/devices/0000:<bus>:<dev>.<func>/resource<bar#> */
#define get_syspath_bar_mmap(s, bus,dev,func,bar) \
	snprintf(s, sizeof(s), \
		"/sys/bus/pci/devices/0000:%02x:%02x.%x/resource%u", \
		bus, dev, func, bar)

static uint32_t *mmap_bar(char *fname, size_t len, int prot)
{
	int fd;
	uint32_t *bar;

	fd = open(fname, (prot & PROT_WRITE) ? O_RDWR : O_RDONLY);
	if (fd < 0)
		return NULL;

	bar = mmap(NULL, len, prot, MAP_SHARED, fd, 0);
	close(fd);

	return bar == MAP_FAILED ? NULL : bar;
}

static uint32_t reg_read_mmap(struct xdev_info *xdev, unsigned char barno,
				uint32_t addr)
{
	uint32_t val, *bar;
	char fname[256];

	get_syspath_bar_mmap(fname, xdev->bus, xdev->dev, xdev->func, barno);

	bar = mmap_bar(fname, addr + 4, PROT_READ);
	if (!bar)
		err(1, "register read");

	val = bar[addr / 4];
	munmap(bar, addr + 4);
	return le32toh(val);
}

static void reg_write_mmap(struct xdev_info *xdev, unsigned char barno,
				uint32_t addr, uint32_t val)
{
	uint32_t *bar;
	char fname[256];

	get_syspath_bar_mmap(fname, xdev->bus, xdev->dev, xdev->func, barno);

	bar = mmap_bar(fname, addr + 4, PROT_WRITE);
	if (!bar)
		err(1, "register write");

	bar[addr / 4] = htole32(val);
	munmap(bar, addr + 4);
}

static void print_repeated_reg(uint32_t *bar, struct xreg_info *xreg,
		unsigned start, unsigned limit) {
	int i;
	int end = start + limit;
	int step = xreg->step ? xreg->step : 4;

	for (i = start; i < end; i++) {
		uint32_t addr = xreg->addr + (i * step);
		uint32_t val = le32toh(bar[addr / 4]);
		char name[40];
		int l = sprintf(name, "%s_%d",
				xreg->name, i);
		printf("[%#7x] %-47s %#-10x %u\n",
			addr, name, val, val);
	}
}

static void reg_dump_mmap(struct xdev_info *xdev, unsigned char barno,
			struct xreg_info *reg_list, unsigned int max)
{
	struct xreg_info *xreg = reg_list;
	uint32_t *bar;
	char fname[256];

	get_syspath_bar_mmap(fname, xdev->bus, xdev->dev, xdev->func, barno);

	bar = mmap_bar(fname, max, PROT_READ);
	if (!bar)
		err(1, "register dump");

	for (xreg = reg_list; strlen(xreg->name); xreg++) {
		if (!xreg->len) {
			if (xreg->repeat) {
				print_repeated_reg(bar, xreg, 0, xreg->repeat);
			} else {
				uint32_t addr = xreg->addr;
				uint32_t val = le32toh(bar[addr / 4]);

				printf("[%#7x] %-47s %#-10x %u\n",
					addr, xreg->name, val, val);
			}
		} else {
			uint32_t addr = xreg->addr;
			uint32_t val = le32toh(bar[addr / 4]);
			uint32_t v = (val >> xreg->shift) &
					((1 << xreg->len) - 1);

			printf("    %*u:%u %-47s %#-10x %u\n",
				xreg->shift < 10 ? 3 : 2,
				xreg->shift + xreg->len - 1,
				xreg->shift, xreg->name, v, v);
		}
	}

	munmap(bar, max);
}

static void reg_dump_range(struct xdev_info *xdev, unsigned char barno,
			 unsigned int max, struct xreg_info *reg_list,
			 unsigned int start, unsigned int limit)
{
	struct xreg_info *xreg = reg_list;
	uint32_t *bar;
	char fname[256];

	get_syspath_bar_mmap(fname, xdev->bus, xdev->dev, xdev->func, barno);

	bar = mmap_bar(fname, max, PROT_READ);
	if (!bar)
		err(1, "register dump");

	for (xreg = reg_list; strlen(xreg->name); xreg++) {
		print_repeated_reg(bar, xreg, start, limit);
	}

	munmap(bar, max);
}

static inline void print_seperator(void)
{
	char buffer[81];

	memset(buffer, '#', 80);
	buffer[80] = '\0';

	fprintf(stdout, "%s\n", buffer);
}

int is_valid_addr(unsigned char bar_no, unsigned int reg_addr)
{
	struct xreg_info *rinfo = (bar_no == 0) ? qdma_config_regs:
			qdma_user_regs;
	unsigned int i;
	unsigned int size = (bar_no == 0) ?
			(sizeof(qdma_config_regs) / sizeof(struct xreg_info)):
			(sizeof(qdma_user_regs) / sizeof(struct xreg_info));

	for (i = 0; i < size; i++)
		if (reg_addr == rinfo[i].addr)
			return 1;

	return 0;
}

int proc_reg_cmd(struct xcmd_info *xcmd)
{
	struct xcmd_reg *regcmd = &xcmd->u.reg;
	struct xdev_info xdev;
	unsigned int mask = (1 << XNL_ATTR_PCI_BUS) | (1 << XNL_ATTR_PCI_DEV) |
			(1 << XNL_ATTR_PCI_FUNC) | (1 << XNL_ATTR_DEV_CFG_BAR) |
			(1 << XNL_ATTR_DEV_USR_BAR);
	unsigned int barno;
	uint32_t v;

	if ((xcmd->attr_mask & mask) != mask) {
		fprintf(stderr, "%s: device info missing, 0x%x/0x%x.\n",
			__FUNCTION__, xcmd->attr_mask, mask);
		return -EINVAL;
	}

	memset(&xdev, 0, sizeof(struct xdev_info));
	xdev.bus = xcmd->attrs[XNL_ATTR_PCI_BUS];
	xdev.dev = xcmd->attrs[XNL_ATTR_PCI_DEV];
	xdev.func = xcmd->attrs[XNL_ATTR_PCI_FUNC];
	xdev.config_bar = xcmd->attrs[XNL_ATTR_DEV_CFG_BAR];
	xdev.user_bar = xcmd->attrs[XNL_ATTR_DEV_USR_BAR];

	barno = (regcmd->sflags & XCMD_REG_F_BAR_SET) ?
			 regcmd->bar : xdev.config_bar;

	switch (xcmd->op) {
	case XNL_CMD_REG_RD:
		v = reg_read_mmap(&xdev, barno, regcmd->reg);
		fprintf(stdout,
			"qdma%d, %02x:%02x.%02x, bar#%u, 0x%x = 0x%x.\n",
			xcmd->if_idx, xdev.bus, xdev.dev, xdev.func, barno,
			regcmd->reg, v);
		break;
	case XNL_CMD_REG_WRT:
		reg_write_mmap(&xdev, barno, regcmd->reg, regcmd->val);
		v = reg_read_mmap(&xdev, barno, regcmd->reg);
		fprintf(stdout,
			"qdma%d, %02x:%02x.%02x, bar#%u, reg 0x%x -> 0x%x, read back 0x%x.\n",
			xcmd->if_idx, xdev.bus, xdev.dev, xdev.func, barno,
			regcmd->reg, regcmd->val, v);
		break;
	case XNL_CMD_REG_DUMP:
		print_seperator();
		fprintf(stdout,
			"###\t\tqdma%d, pci %02x:%02x.%02x, reg dump\n",
			xcmd->if_idx, xdev.bus, xdev.dev, xdev.func);
		print_seperator();

		fprintf(stdout, "\nUSER BAR #%d\n", xdev.user_bar);
		reg_dump_mmap(&xdev, xdev.user_bar, qdma_user_regs,
				QDMA_USR_BAR_SIZE);

		fprintf(stdout, "\nCONFIG BAR #%d\n", xdev.config_bar);
		reg_dump_mmap(&xdev, xdev.config_bar, qdma_config_regs,
				QDMA_CFG_BAR_SIZE);
		reg_dump_range(&xdev, xdev.config_bar, QDMA_CFG_BAR_SIZE,
				qdma_dmap_regs, regcmd->range_start,
				regcmd->range_end);
		break;
	default:
		break;
	}
	return 0;
}
