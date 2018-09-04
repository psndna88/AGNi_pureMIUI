/*
 * Sigma Control API DUT (server)
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sqlite3.h>

#ifndef SERVER_DB
#define SERVER_DB "/home/user/hs20-server/AS/DB/eap_user.db"
#endif /* SERVER_DB */


static int cmd_server_ca_get_version(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd)
{
	send_resp(dut, conn, SIGMA_COMPLETE, "version,1.0");
	return 0;
}


static int cmd_server_get_info(struct sigma_dut *dut,
			       struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	send_resp(dut, conn, SIGMA_COMPLETE, "vendor,OSU,model,OS,version,1.0");
	return 0;
}


static int server_reset_user(struct sigma_dut *dut, const char *user)
{
	sqlite3 *db;
	int res = -1;
	char *sql = NULL;
	const char *realm = "wi-fi.org";
	const char *methods = "TTLS-MSCHAPV2";
	const char *password = "ChangeMe";
	int phase2 = 1;
	int machine_managed = 1;
	int remediation = 0;
	int fetch_pps = 0;
	const char *osu_user = NULL;
	const char *osu_password = NULL;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Reset user %s", user);

	if (sqlite3_open(SERVER_DB, &db)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open SQLite database %s",
				SERVER_DB);
		return -1;
	}

	if (strcmp(user, "test01") == 0) {
	} else if (strcmp(user, "test02") == 0) {
		machine_managed = 0;
	} else if (strcmp(user, "test03") == 0) {
	} else if (strcmp(user, "test04") == 0) {
	} else if (strcmp(user, "test05") == 0) {
	} else if (strcmp(user, "test06") == 0) {
		realm = "example.com";
	} else if (strcmp(user, "test07") == 0) {
	} else if (strcmp(user, "test08") == 0) {
		osu_user = "testdmacc08";
		osu_password = "P@ssw0rd";
	} else if (strcmp(user, "test09") == 0) {
	} else if (strcmp(user, "test10") == 0) {
		methods = "TLS";
	} else if (strcmp(user, "test11") == 0) {
	} else if (strcmp(user, "test12") == 0) {
		methods = "TLS";
	} else if (strcmp(user, "test20") == 0) {
	} else if (strcmp(user, "test26") == 0) {
		/* TODO: Cred01 with username/password? */
		user = "1310026000000001";
		methods = "SIM";
	} else if (strcmp(user, "test30") == 0) {
		osu_user = "testdmacc30";
		osu_password = "P@ssw0rd";
	} else if (strcmp(user, "test31") == 0) {
		osu_user = "testdmacc31";
		osu_password = "P@ssw0rd";
	} else if (strcmp(user, "test32") == 0) {
		osu_user = "testdmacc32";
		osu_password = "P@ssw0rd";
	} else if (strcmp(user, "test33") == 0) {
		osu_user = "testdmacc33";
		osu_password = "P@ssw0rd";
	} else if (strcmp(user, "test34") == 0) {
		osu_user = "testdmacc34";
		osu_password = "P@ssw0rd";
	} else if (strcmp(user, "test35") == 0) {
		osu_user = "testdmacc35";
		osu_password = "P@ssw0rd";
	} else if (strcmp(user, "test36") == 0) {
	} else if (strcmp(user, "test37") == 0) {
		osu_user = "testdmacc37";
		osu_password = "P@ssw0rd";
	} else {
		sigma_dut_print(dut, DUT_MSG_INFO, "Unsupported username '%s'",
				user);
		goto fail;
	}

	sql = sqlite3_mprintf("INSERT OR REPLACE INTO users(identity,realm,methods,password,phase2,machine_managed,remediation,fetch_pps,osu_user,osu_password) VALUES (%Q,%Q,%Q,%Q,%d,%d,%d,%d,%Q,%Q)",
			      user, realm, methods, password,
			      phase2, machine_managed, remediation, fetch_pps,
			      osu_user, osu_password);

	if (!sql)
		goto fail;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "SQL: %s", sql);

	if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "SQL operation failed: %s",
				sqlite3_errmsg(db));
	} else {
		res = 0;
	}

	sqlite3_free(sql);

fail:
	sqlite3_close(db);

	return res;
}


static int cmd_server_reset_default(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *var;
	enum sigma_program prog;

	var = get_param(cmd, "Program");
	if (!var) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing program parameter");
		return 0;
	}

	prog = sigma_program_to_enum(var);
	if (prog != PROGRAM_HS2_R2 && prog != PROGRAM_HS2_R3) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported program");
		return 0;
	}

	var = get_param(cmd, "UserName");
	if (var && server_reset_user(dut, var) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to reset user account to defaults");
		return 0;
	}

	var = get_param(cmd, "SerialNo");
	if (var) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Reset serial number %s",
				var);
		/* TODO */
	}

	return 1;
}


static int cmd_server_request_status(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd)
{
	const char *var, *username, *serialno, *imsi, *addr, *status;
	int osu, timeout;
	char resp[500];

	var = get_param(cmd, "Program");
	if (var == NULL || strcasecmp(var, "HS2-R2") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported program");
		return 0;
	}

	var = get_param(cmd, "Device");
	if (!var ||
	    (strcasecmp(var, "AAAServer") != 0 &&
	     strcasecmp(var, "OSUServer") != 0)) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported device type");
		return 0;
	}
	osu = strcasecmp(var, "OSUServer") == 0;

	var = get_param(cmd, "Timeout");
	if (!var) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing timeout");
		return 0;
	}
	timeout = atoi(var);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "timeout: %d", timeout);

	username = get_param(cmd, "UserName");
	if (username)
		sigma_dut_print(dut, DUT_MSG_DEBUG, "UserName: %s", username);
	serialno = get_param(cmd, "SerialNo");
	if (serialno)
		sigma_dut_print(dut, DUT_MSG_DEBUG, "SerialNo: %s", serialno);
	imsi = get_param(cmd, "imsi_val");
	if (imsi)
		sigma_dut_print(dut, DUT_MSG_DEBUG, "imsi_val: %s", imsi);
	addr = get_param(cmd, "ClientMACAddr");
	if (addr)
		sigma_dut_print(dut, DUT_MSG_DEBUG, "ClientMACAddr: %s", addr);
	status = get_param(cmd, "Status");
	if (status)
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Status: %s", status);

	if (osu && status && strcasecmp(status, "Remediation") == 0) {
		/* TODO */
		sleep(1);
		snprintf(resp, sizeof(resp),
			 "RemediationStatus,Remediation Complete");
		send_resp(dut, conn, SIGMA_COMPLETE, resp);
		return 0;
	}

	return 1;
}


void server_register_cmds(void)
{
	sigma_dut_reg_cmd("server_ca_get_version", NULL,
			  cmd_server_ca_get_version);
	sigma_dut_reg_cmd("server_get_info", NULL,
			  cmd_server_get_info);
	sigma_dut_reg_cmd("server_reset_default", NULL,
			  cmd_server_reset_default);
	sigma_dut_reg_cmd("server_request_status", NULL,
			  cmd_server_request_status);
}
