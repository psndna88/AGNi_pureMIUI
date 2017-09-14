/*
 * Sigma Control API DUT (station/AP/sniffer)
 * Copyright (c) 2011-2013, 2017, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include "miracast.h"


static int cmd_dev_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
#ifdef MIRACAST
	const char *program = get_param(cmd, "Program");

	if (program && (strcasecmp(program, "WFD") == 0 ||
			strcasecmp(program, "DisplayR2") == 0))
		return miracast_dev_send_frame(dut, conn, cmd);
#endif /* MIRACAST */

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


static int cmd_dev_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *program = get_param(cmd, "Program");

#ifdef MIRACAST
	if (program && (strcasecmp(program, "WFD") == 0 ||
			strcasecmp(program, "DisplayR2") == 0)) {
		if (get_param(cmd, "interface") == NULL)
			return -1;
		return miracast_dev_exec_action(dut, conn, cmd);
	}
#endif /* MIRACAST */

	if (program && strcasecmp(program, "DPP") == 0)
		return dpp_dev_exec_action(dut, conn, cmd);

	return -2;
}


static int cmd_dev_configure_ie(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *ie_name = get_param(cmd, "IE_Name");
	const char *contents = get_param(cmd, "Contents");

	if (!ie_name || !contents)
		return -1;

	if (strcasecmp(ie_name, "RSNE") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported IE_Name value");
		return 0;
	}

	free(dut->rsne_override);
	dut->rsne_override = strdup(contents);

	return dut->rsne_override ? 1 : -1;
}


static int req_intf(struct sigma_cmd *cmd)
{
	return get_param(cmd, "interface") == NULL ? -1 : 0;
}


static int req_intf_prog(struct sigma_cmd *cmd)
{
	if (get_param(cmd, "interface") == NULL)
		return -1;
	if (get_param(cmd, "program") == NULL)
		return -1;
	return 0;
}


static int req_prog(struct sigma_cmd *cmd)
{
	if (get_param(cmd, "program") == NULL)
		return -1;
	return 0;
}


void dev_register_cmds(void)
{
	sigma_dut_reg_cmd("dev_send_frame", req_intf_prog, cmd_dev_send_frame);
	sigma_dut_reg_cmd("dev_set_parameter", req_intf_prog,
			  cmd_dev_set_parameter);
	sigma_dut_reg_cmd("dev_exec_action", req_prog,
			  cmd_dev_exec_action);
	sigma_dut_reg_cmd("dev_configure_ie", req_intf, cmd_dev_configure_ie);
}
