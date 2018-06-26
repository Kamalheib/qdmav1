#ifndef USER_CLI_CMD_PARSE_H_
#define USER_CLI_CMD_PARSE_H_

#include "qdma_nl.h"
#include "reg_cmd.h"
#include "nl_user.h"

struct xcmd_info {
	unsigned char vf:1;
	unsigned char op:7;
	unsigned char if_idx;
	unsigned char config_bar;
	unsigned char user_bar;
	unsigned short qmax;
	char ifname[8];
	union {
		struct xcmd_intr intr;
		struct xcmd_reg reg;
		struct xcmd_q_parm qparm;
	} u;
	uint32_t attr_mask;
	uint32_t attrs[XNL_ATTR_MAX];
	char drv_str[128];
};

int parse_cmd(int argc, char *argv[], struct xcmd_info *xcmd);

#endif /* USER_CLI_CMD_PARSE_H_ */
