/*
 * Sigma Control API DUT (station/AP/sniffer)
 * Copyright (c) 2011-2013, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"


static int cmd_dev_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	if (dut->mode == SIGMA_MODE_STATION ||
	    dut->mode == SIGMA_MODE_UNKNOWN) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Convert "
				"dev_send_frame to sta_send_frame");
		return cmd_sta_send_frame(dut, conn, cmd);
	}

	if (dut->mode == SIGMA_MODE_AP) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Convert "
				"dev_send_frame to ap_send_frame");
		return cmd_ap_send_frame(dut, conn, cmd);
	}

#ifdef CONFIG_WLANTEST
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Convert dev_send_frame to "
			"wlantest_send_frame");
	return cmd_wlantest_send_frame(dut, conn, cmd);
#else /* CONFIG_WLANTEST */
	send_resp(dut, conn, SIGMA_ERROR,
		  "errorCode,Unsupported dev_send_frame");
	return 0;
#endif /* CONFIG_WLANTEST */
}


static int cmd_dev_set_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *device = get_param(cmd, "Device");

	if (device && strcasecmp(device, "STA") == 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Convert "
				"dev_set_parameter to sta_set_parameter");
		return cmd_sta_set_parameter(dut, conn, cmd);
	}

	return -1;
}


static int req_intf_prog(struct sigma_cmd *cmd)
{
	if (get_param(cmd, "interface") == NULL)
		return -1;
	if (get_param(cmd, "program") == NULL)
		return -1;
	return 0;
}


void dev_register_cmds(void)
{
	sigma_dut_reg_cmd("dev_send_frame", req_intf_prog, cmd_dev_send_frame);
	sigma_dut_reg_cmd("dev_set_parameter", req_intf_prog,
			  cmd_dev_set_parameter);
}
