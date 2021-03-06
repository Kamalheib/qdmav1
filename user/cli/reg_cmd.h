#ifndef USER_CLI_REG_CMD_H_
#define USER_CLI_REG_CMD_H_

#include <stdint.h>

struct xcmd_info;

struct xcmd_reg {
	unsigned int sflags;
#define XCMD_REG_F_BAR_SET	0x1
#define XCMD_REG_F_REG_SET	0x2
#define XCMD_REG_F_VAL_SET	0x4
	unsigned int bar;
	unsigned int reg;
	unsigned int val;
	unsigned int range_start;
	unsigned int range_end;
};

struct xreg_info {
	const char name[32];
	uint32_t addr;
	unsigned int repeat;
	unsigned int step;
	unsigned char shift;
	unsigned char len;
	unsigned char filler[2];
};

int proc_reg_cmd(struct xcmd_info *xcmd);
int is_valid_addr(unsigned char bar_no, unsigned int reg_addr);

#endif /* USER_CLI_REG_CMD_H_ */
