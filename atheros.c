/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 * Copyright (c) 2019, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include "wpa_helpers.h"


static enum sigma_cmd_result cmd_sta_atheros(struct sigma_dut *dut,
					     struct sigma_conn *conn,
					     struct sigma_cmd *cmd)
{
	char buf[2048], *pos;
	int i;
	const char *intf, *c;
	char resp[200];

	intf = get_param(cmd, "interface");
	c = get_param(cmd, "cmd");
	if (c == NULL)
		return INVALID_SEND_STATUS;

	buf[0] = '\0';
	if (strncmp(c, "ctrl=", 5) == 0) {
		size_t rlen;
		c += 5;
		if (wpa_command_resp(intf, c, buf, sizeof(buf)) < 0)
			return ERROR_SEND_STATUS;
		rlen = strlen(buf);
		if (rlen > 0 && buf[rlen - 1] == '\n')
			buf[rlen - 1] = '\0';
	} else if (strncmp(c, "timeout=", 8) == 0) {
		unsigned int timeout;
		timeout = atoi(c + 8);
		if (timeout == 0)
			return INVALID_SEND_STATUS;
		dut->default_timeout = timeout;
		sigma_dut_print(dut, DUT_MSG_INFO, "Set DUT default timeout "
				"to %u seconds", dut->default_timeout);
		snprintf(buf, sizeof(buf), "OK");
	} else
		return ERROR_SEND_STATUS;

	i = snprintf(resp, sizeof(resp), "resp,");
	if (i < 0)
		return ERROR_SEND_STATUS;
	pos = buf;
	while (*pos && i + 1 < (int) sizeof(resp)) {
		char c = *pos++;
		if (c == '\n' || c == '\r' || c == ',')
			c = '^';
		resp[i++] = c;
	}
	resp[i] = '\0';

	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return STATUS_SENT;
}


static int req_intf(struct sigma_cmd *cmd)
{
	return get_param(cmd, "interface") == NULL ? -1 : 0;
}


#ifdef NL80211_SUPPORT
static enum sigma_cmd_result cmd_atheros_config_scan(struct sigma_dut *dut,
						     struct sigma_conn *conn,
						     struct sigma_cmd *cmd)
{
	struct nl_msg *msg;
	int ret;
	struct nlattr *params;
	const char *val;
	int ifindex;

	val = get_param(cmd, "enable");
	if (!val)
		return INVALID_SEND_STATUS;
	ifindex = if_nametoindex("wlan0");
	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_CONFIGURATION) ||
	    !(params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    nla_put_u8(msg,
		       QCA_WLAN_VENDOR_ATTR_CONFIG_SCAN_ENABLE,
		       atoi(val))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		return ERROR_SEND_STATUS;
	}
	nla_nest_end(msg, params);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
		return ERROR_SEND_STATUS;
	}

	return STATUS_SENT;
}
#endif /* NL80211_SUPPORT */


void atheros_register_cmds(void)
{
	sigma_dut_reg_cmd("sta_atheros", req_intf, cmd_sta_atheros);
#ifdef NL80211_SUPPORT
	sigma_dut_reg_cmd("atheros_config_scan", NULL, cmd_atheros_config_scan);
#endif /* NL80211_SUPPORT */
}
