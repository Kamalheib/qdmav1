
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/genetlink.h>
#include <qdma_nl.h>

#include "nl_user.h"
#include "cmd_parse.h"

/* Generic macros for dealing with netlink sockets. Might be duplicated
 * elsewhere. It is recommended that commercial grade applications use
 * libnl or libnetlink and use the interfaces provided by the library
 */

/*
 * netlink message
 */
struct xnl_hdr {
	struct nlmsghdr n;
	struct genlmsghdr g;
};

struct xnl_gen_msg {
	struct xnl_hdr hdr;
	char data[0];
};

void xnl_close(struct xnl_cb *cb)
{
	close(cb->fd);
}

static int xnl_send(struct xnl_cb *cb, struct xnl_hdr *hdr)
{
	int rv;
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
	};

	hdr->n.nlmsg_seq = cb->snd_seq;
	cb->snd_seq++;

	rv = sendto(cb->fd, (char *)hdr, hdr->n.nlmsg_len, 0,
			(struct sockaddr *)&addr, sizeof(addr));
        if (rv != hdr->n.nlmsg_len) {
		perror("nl send err");
		return -1;
	}

	return 0;
}

static int xnl_recv(struct xnl_cb *cb, struct xnl_hdr *hdr, int dlen, int print)
{
	int rv;
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
	};

	memset(hdr, 0, sizeof(struct xnl_gen_msg) + dlen);

	rv = recv(cb->fd, hdr, dlen, 0);
	if (rv < 0) {
		perror("nl recv err");
		return -1;
	}
	/* as long as there is attribute, even if it is shorter than expected */
	if (!NLMSG_OK((&hdr->n), rv) && (rv <= sizeof(struct xnl_hdr))) {
		if (print)
			fprintf(stderr,
				"nl recv:, invalid message, cmd 0x%x, %d,%d.\n",
				hdr->g.cmd, dlen, rv);
		return -1;
	}

	if (hdr->n.nlmsg_type == NLMSG_ERROR) {
		if (print)
			fprintf(stderr, "nl recv, msg error, cmd 0x%x\n",
				hdr->g.cmd);
		return -1;
	}

	return 0;
}

static inline struct xnl_gen_msg *xnl_msg_alloc(int dlen)
{
	struct xnl_gen_msg *msg;

	msg = malloc(sizeof(struct xnl_gen_msg) + dlen);
	if (!msg) {
		fprintf(stderr, "%s: OOM, %d.\n", __FUNCTION__, dlen);
		return NULL;
	}

	memset(msg, 0, sizeof(struct xnl_gen_msg) + dlen);
	return msg;
}

int xnl_connect(struct xnl_cb *cb, int vf)
{
	int fd;	
	struct sockaddr_nl addr;
	struct xnl_gen_msg *msg = xnl_msg_alloc(XNL_RESP_BUFLEN_MIN);
	struct xnl_hdr *hdr;
	struct nlattr *attr;
	int rv = -1;

	if (!msg) {
		fprintf(stderr, "%s, msg OOM.\n", __FUNCTION__);
		return -ENOMEM;
	}
	hdr = (struct xnl_hdr *)msg;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (fd < 0) {
                perror("nl socket err");
		rv = fd;
		goto out;
        }
	cb->fd = fd;

	memset(&addr, 0, sizeof(struct sockaddr_nl));	
	addr.nl_family = AF_NETLINK;
	rv = bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_nl));
	if (rv < 0) {
		perror("nl bind err");
		goto out;
	}

	hdr->n.nlmsg_type = GENL_ID_CTRL;
	hdr->n.nlmsg_flags = NLM_F_REQUEST;
	hdr->n.nlmsg_pid = getpid();
	hdr->n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);

        hdr->g.cmd = CTRL_CMD_GETFAMILY;
        hdr->g.version = XNL_VERSION;

	attr = (struct nlattr *)(msg->data);
	attr->nla_type = CTRL_ATTR_FAMILY_NAME;

	if (vf) {
        	attr->nla_len = strlen(XNL_NAME_VF) + 1 + NLA_HDRLEN;
        	strcpy((char *)(attr + 1), XNL_NAME_VF);

	} else {
        	attr->nla_len = strlen(XNL_NAME_PF) + 1 + NLA_HDRLEN;
        	strcpy((char *)(attr + 1), XNL_NAME_PF);
	}
        hdr->n.nlmsg_len += NLMSG_ALIGN(attr->nla_len);

	rv = xnl_send(cb, (struct xnl_hdr *)hdr);	
	if (rv < 0)
		goto out;

	rv = xnl_recv(cb, hdr, XNL_RESP_BUFLEN_MIN, 0);
	if (rv < 0)
		goto out;

#if 0
	/* family name */
        if (attr->nla_type == CTRL_ATTR_FAMILY_NAME)
		printf("family name: %s.\n", (char *)(attr + 1));
#endif

	attr = (struct nlattr *)((char *)attr + NLA_ALIGN(attr->nla_len));
	/* family ID */
        if (attr->nla_type == CTRL_ATTR_FAMILY_ID) {
		cb->family = *(__u16 *)(attr + 1);
//		printf("family id: 0x%x.\n", cb->family);
	}

	rv = 0;

out:
	free(msg);
	return rv;
}

static void xnl_msg_set_hdr(struct xnl_hdr *hdr, int family, int op)
{
	hdr->n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	hdr->n.nlmsg_type = family;
	hdr->n.nlmsg_flags = NLM_F_REQUEST;
	hdr->n.nlmsg_pid = getpid();

	hdr->g.cmd = op;
}

static int xnl_send_op(struct xnl_cb *cb, int op)
{
	struct xnl_hdr req;
	int rv;

	memset(&req, 0, sizeof(struct xnl_hdr));

	xnl_msg_set_hdr(&req, cb->family, op);

	rv = xnl_send(cb, &req);	

	return rv;
}

static int xnl_msg_add_int_attr(struct xnl_hdr *hdr, enum xnl_attr_t type,
				unsigned int v)
{
	struct nlattr *attr = (struct nlattr *)((char *)hdr + hdr->n.nlmsg_len);

        attr->nla_type = (__u16)type;
        attr->nla_len = sizeof(__u32) + NLA_HDRLEN;
	*(__u32 *)(attr+ 1) = v;

        hdr->n.nlmsg_len += NLMSG_ALIGN(attr->nla_len);
	return 0;
}

static int xnl_msg_add_str_attr(struct xnl_hdr *hdr, enum xnl_attr_t type,
				char *s)
{
	struct nlattr *attr = (struct nlattr *)((char *)hdr + hdr->n.nlmsg_len);
	int len = strlen(s);	

        attr->nla_type = (__u16)type;
        attr->nla_len = len + 1 + NLA_HDRLEN;

	strcpy((char *)(attr + 1), s);

        hdr->n.nlmsg_len += NLMSG_ALIGN(attr->nla_len);
	return 0;
}

static int recv_attrs(struct xnl_hdr *hdr, struct xcmd_info *xcmd)
{
	unsigned char *p = (unsigned char *)(hdr + 1);
	int maxlen = hdr->n.nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);

#if 0
	printf("nl recv, hdr len %d, data %d, gen op 0x%x, %s, ver 0x%x.\n",
		hdr->n.nlmsg_len, maxlen, hdr->g.cmd, xnl_op_str[hdr->g.cmd],
		hdr->g.version);
#endif

	xcmd->attr_mask = 0;
	while (maxlen > 0) {
		struct nlattr *na = (struct nlattr *)p;
		int len = NLA_ALIGN(na->nla_len);

		if (na->nla_type >= XNL_ATTR_MAX) {
			fprintf(stderr, "unknown attr type %d, len %d.\n",
				na->nla_type, na->nla_len);
			return -EINVAL;
		}

		xcmd->attr_mask |= 1 << na->nla_type;

		if (na->nla_type == XNL_ATTR_GENMSG) {
			printf("\n%s\n", (char *)(na + 1));

		} else if (na->nla_type == XNL_ATTR_DRV_INFO) {
			strncpy(xcmd->drv_str, (char *)(na + 1), 128);
		} else {
			xcmd->attrs[na->nla_type] = *(uint32_t *)(na + 1);
		}

		p += len;
		maxlen -= len;
	}

	return 0;
}

static int recv_nl_msg(struct xnl_hdr *hdr, struct xcmd_info *xcmd)
{
	unsigned int op = hdr->g.cmd;
	unsigned int usr_bar;

	recv_attrs(hdr, xcmd);

	switch(op) {
	case XNL_CMD_DEV_LIST:
		break;
	case XNL_CMD_DEV_INFO:
		xcmd->config_bar = xcmd->attrs[XNL_ATTR_DEV_CFG_BAR];
		usr_bar = (int)xcmd->attrs[XNL_ATTR_DEV_USR_BAR];
		xcmd->qmax = xcmd->attrs[XNL_ATTR_DEV_QSET_MAX];

		if (usr_bar+1 == 0)
			xcmd->user_bar = 2;
		else
			xcmd->user_bar = usr_bar;

		printf("qdma%s%d:\t%02x:%02x.%02x\t",
			xcmd->vf ? "vf" : "", xcmd->if_idx,
			xcmd->attrs[XNL_ATTR_PCI_BUS],
			xcmd->attrs[XNL_ATTR_PCI_DEV],
			xcmd->attrs[XNL_ATTR_PCI_FUNC]);
		printf("config bar: %d, user bar: %d, max #. QP: %d\n",
			xcmd->config_bar, xcmd->user_bar, xcmd->qmax);
		break;
	case XNL_CMD_Q_LIST:
		break;
	case XNL_CMD_Q_ADD:
		break;
	case XNL_CMD_Q_START:
		break;
	case XNL_CMD_Q_STOP:
		break;
	case XNL_CMD_Q_DEL:
		break;
	default:
		break;
	}

	return 0;
}

static void xnl_msg_add_extra_config_attrs(struct xnl_hdr *hdr,
                                       struct xcmd_info *xcmd)
{
	if (xcmd->u.qparm.sflags & (1 << QPARM_RNGSZ_IDX))
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QRNGSZ_IDX,
		                     xcmd->u.qparm.qrngsz_idx);
	if (xcmd->u.qparm.sflags & (1 << QPARM_C2H_BUFSZ_IDX))
		xnl_msg_add_int_attr(hdr, XNL_ATTR_C2H_BUFSZ_IDX,
		                     xcmd->u.qparm.c2h_bufsz_idx);
	if (xcmd->u.qparm.sflags & (1 << QPARM_WRBSZ))
		xnl_msg_add_int_attr(hdr,  XNL_ATTR_WRB_DESC_SIZE,
		                     xcmd->u.qparm.wrb_entry_size);
	if (xcmd->u.qparm.sflags & (1 << QPARM_WRB_TMR_IDX))
		xnl_msg_add_int_attr(hdr,  XNL_ATTR_WRB_TIMER_IDX,
		                     xcmd->u.qparm.wrb_tmr_idx);
	if (xcmd->u.qparm.sflags & (1 << QPARM_WRB_CNTR_IDX))
		xnl_msg_add_int_attr(hdr,  XNL_ATTR_WRB_CNTR_IDX,
		                     xcmd->u.qparm.wrb_cntr_idx);
	if (xcmd->u.qparm.sflags & (1 << QPARM_WRB_TRIG_MODE))
		xnl_msg_add_int_attr(hdr,  XNL_ATTR_WRB_TRIG_MODE,
		                     xcmd->u.qparm.wrb_trig_mode);
}

static int get_cmd_resp_buf_len(struct xcmd_info *xcmd)
{
	int buf_len = XNL_RESP_BUFLEN_MAX;
	unsigned int row_len = 50;

	switch (xcmd->op) {
	        case XNL_CMD_Q_DESC:
	        	row_len *= 2;
	        case XNL_CMD_Q_WRB:
	        	buf_len += ((xcmd->u.qparm.range_end -
	        			xcmd->u.qparm.range_start)*row_len);
	        	break;
	        case XNL_CMD_INTR_RING_DUMP:
	        	buf_len += ((xcmd->u.intr.end_idx -
					     xcmd->u.intr.start_idx)*row_len);
	        	break;
	        case XNL_CMD_DEV_LIST:
	        case XNL_CMD_Q_LIST:
	        case XNL_CMD_Q_DUMP:
	        case XNL_CMD_Q_ADD:
	        	break;
	        default:
	        	buf_len = XNL_RESP_BUFLEN_MIN;
	        	break;
	}

	return buf_len;
}

int xnl_send_cmd(struct xnl_cb *cb, struct xcmd_info *xcmd)
{
	struct xnl_gen_msg *msg;
	struct xnl_hdr *hdr;
	struct nlattr *attr;
	int dlen = get_cmd_resp_buf_len(xcmd);
	int i;
	int rv;
	xnl_st_c2h_wrb_desc_size wrb_desc_size;

#if 0
	printf("%s: op %s, 0x%x, ifname %s.\n", __FUNCTION__,
		xnl_op_str[xcmd->op], xcmd->op, xcmd->ifname);
#endif

	msg = xnl_msg_alloc(dlen);
	if (!msg) {
		fprintf(stderr, "%s: OOM, %s, op %s,0x%x.\n", __FUNCTION__,
			xcmd->ifname, xnl_op_str[xcmd->op], xcmd->op);
		return -ENOMEM;
	}

	hdr = (struct xnl_hdr *)msg;
	attr = (struct nlattr *)(msg->data);

	xnl_msg_set_hdr(hdr, cb->family, xcmd->op);

	xnl_msg_add_int_attr(hdr, XNL_ATTR_DEV_IDX, xcmd->if_idx);

	switch(xcmd->op) {
        case XNL_CMD_DEV_LIST:
        case XNL_CMD_DEV_INFO:
        case XNL_CMD_Q_LIST:
		/* no parameter */
		break;
        case XNL_CMD_Q_ADD:
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QIDX, xcmd->u.qparm.idx);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_NUM_Q, xcmd->u.qparm.num_q);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QFLAG, xcmd->u.qparm.flags);
		break;
        case XNL_CMD_Q_START:
        	xnl_msg_add_extra_config_attrs(hdr, xcmd);
        case XNL_CMD_Q_STOP:
        case XNL_CMD_Q_DEL:
        case XNL_CMD_Q_DUMP:
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QIDX, xcmd->u.qparm.idx);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_NUM_Q, xcmd->u.qparm.num_q);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QFLAG, xcmd->u.qparm.flags);
		break;
        case XNL_CMD_Q_DESC:
        case XNL_CMD_Q_WRB:
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QIDX, xcmd->u.qparm.idx);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_NUM_Q, xcmd->u.qparm.num_q);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QFLAG, xcmd->u.qparm.flags);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_RANGE_START,
					xcmd->u.qparm.range_start);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_RANGE_END,
					xcmd->u.qparm.range_end);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_RSP_BUF_LEN, dlen);
		break;
        case XNL_CMD_Q_RX_PKT:
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QIDX, xcmd->u.qparm.idx);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_NUM_Q, xcmd->u.qparm.num_q);
		/* hard coded to C2H */
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QFLAG, XNL_F_QDIR_C2H);
		break;
        case XNL_CMD_INTR_RING_DUMP:
		xnl_msg_add_int_attr(hdr, XNL_ATTR_INTR_VECTOR_IDX,
		                     xcmd->u.intr.vector);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_INTR_VECTOR_START_IDX,
		                     xcmd->u.intr.start_idx);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_INTR_VECTOR_END_IDX,
		                     xcmd->u.intr.end_idx);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_RSP_BUF_LEN, dlen);
		break;
#ifdef ERR_DEBUG
        case XNL_CMD_Q_ERR_INDUCE:
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QIDX, xcmd->u.qparm.idx);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QFLAG, xcmd->u.qparm.flags);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QPARAM_ERR_SEL1,
		                     xcmd->u.qparm.err_sel[0]);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QPARAM_ERR_SEL2,
		                     xcmd->u.qparm.err_sel[1]);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QPARAM_ERR_MASK1,
		                     xcmd->u.qparm.err_mask[0]);
		xnl_msg_add_int_attr(hdr, XNL_ATTR_QPARAM_ERR_MASK2,
		                     xcmd->u.qparm.err_mask[1]);
		break;
#endif
	default:
		break;
	}

	rv = xnl_send(cb, hdr);	
	if (rv < 0)
		goto out;

	rv = xnl_recv(cb, hdr, dlen, 1);
	if (rv < 0)
		goto out;

	rv = recv_nl_msg(hdr, xcmd);
out:
	free(msg);
	return rv;
}
