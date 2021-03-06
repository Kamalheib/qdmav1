#include <string.h>

#include "nl_user.h"
#include "cmd_parse.h"
#include "reg_cmd.h"

int main(int argc, char *argv[])
{
	struct xnl_cb cb;
	struct xcmd_info xcmd;
	unsigned char op;
	int rv = 0;

	memset(&xcmd, 0, sizeof(xcmd));

	rv = parse_cmd(argc, argv, &xcmd);
	if (rv < 0)
		return rv;

#if 0
	printf("cmd op %s, 0x%x, ifname %s.\n",
		xnl_op_str[xcmd.op], xcmd.op, xcmd.ifname);
#endif

	memset(&cb, 0, sizeof(struct xnl_cb));

	if (xcmd.op == XNL_CMD_DEV_LIST) {
		/* try pf nl server */
		rv = xnl_connect(&cb, 0);
		if (!rv)
			rv = xnl_send_cmd(&cb, &xcmd);
		xnl_close(&cb);

		/* try vf nl server */
		memset(&cb, 0, sizeof(struct xnl_cb));
		rv = xnl_connect(&cb, 1);
		if (!rv)
			rv = xnl_send_cmd(&cb, &xcmd);
		xnl_close(&cb);

		goto close;
	}

	/* for all other command, query the target device info first */
	rv = xnl_connect(&cb, xcmd.vf);
	if (rv < 0)
		goto close;

 	op = xcmd.op;
	xcmd.op = XNL_CMD_DEV_INFO;
	rv = xnl_send_cmd(&cb, &xcmd);
	xcmd.op = op;
	if (rv < 0)
		goto close;

	if ((xcmd.op == XNL_CMD_REG_DUMP) || (xcmd.op == XNL_CMD_REG_RD) ||
	    (xcmd.op == XNL_CMD_REG_WRT))
		rv = proc_reg_cmd(&xcmd);
	else
		rv = xnl_send_cmd(&cb, &xcmd);

close:
	xnl_close(&cb);
	return rv;
}
