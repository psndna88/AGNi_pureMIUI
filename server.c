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

#ifndef CERT_DIR
#define CERT_DIR "/home/user/hs20-server/certs"
#endif /* CERT_DIR */


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
	const char *remediation = "";
	int fetch_pps = 0;
	const char *osu_user = NULL;
	const char *osu_password = NULL;
	const char *policy = NULL;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Reset user %s", user);

	if (sqlite3_open(SERVER_DB, &db)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open SQLite database %s",
				SERVER_DB);
		return -1;
	}

	if (strcmp(user, "test01") == 0) {
		remediation = "machine";
	} else if (strcmp(user, "test02") == 0) {
		remediation = "user";
		machine_managed = 0;
	} else if (strcmp(user, "test03") == 0) {
		/* UpdateInterval-based client trigger for policy update */
		policy = "ruckus130";
	} else if (strcmp(user, "test04") == 0) {
	} else if (strcmp(user, "test05") == 0) {
	} else if (strcmp(user, "test06") == 0) {
		realm = "example.com";
	} else if (strcmp(user, "test07") == 0) {
	} else if (strcmp(user, "test08") == 0) {
		remediation = "machine";
		osu_user = "testdmacc08";
		osu_password = "P@ssw0rd";
	} else if (strcmp(user, "test09") == 0) {
		/* UpdateInterval-based client trigger for policy update */
		policy = "ruckus130";
		osu_user = "testdmacc09";
		osu_password = "P@ssw0rd";
	} else if (strcmp(user, "test10") == 0) {
		remediation = "machine";
		methods = "TLS";
	} else if (strcmp(user, "test11") == 0) {
	} else if (strcmp(user, "test12") == 0) {
		remediation = "user";
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
	} else if (strcmp(user, "testdmacc08") == 0 ||
		   strcmp(user, "testdmacc09") == 0) {
		/* No need to set anything separate for testdmacc* users */
		sqlite3_close(db);
		return 0;
	} else {
		sigma_dut_print(dut, DUT_MSG_INFO, "Unsupported username '%s'",
				user);
		goto fail;
	}

	sql = sqlite3_mprintf("INSERT OR REPLACE INTO users(identity,realm,methods,password,phase2,machine_managed,remediation,fetch_pps,osu_user,osu_password,policy) VALUES (%Q,%Q,%Q,%Q,%d,%d,%Q,%d,%Q,%Q,%Q)",
			      user, realm, methods, password,
			      phase2, machine_managed, remediation, fetch_pps,
			      osu_user, osu_password, policy);

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


static int server_reset_cert_enroll(struct sigma_dut *dut, const char *addr)
{
	sqlite3 *db;
	char *sql;

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Reset certificate enrollment status for %s", addr);

	if (sqlite3_open(SERVER_DB, &db)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open SQLite database %s",
				SERVER_DB);
		return -1;
	}
	sql = sqlite3_mprintf("DELETE FROM cert_enroll WHERE mac_addr=%Q",
			      addr);
	if (!sql) {
		sqlite3_close(db);
		return -1;
	}
	sigma_dut_print(dut, DUT_MSG_DEBUG, "SQL: %s", sql);

	if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"SQL operation failed: %s",
				sqlite3_errmsg(db));
		sqlite3_free(sql);
		sqlite3_close(db);
		return -1;
	}

	sqlite3_free(sql);
	sqlite3_close(db);

	return 0;
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

	var = get_param(cmd, "ClientMACAddr");
	if (var && server_reset_cert_enroll(dut, var) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to reset cert enroll to defaults");
		return 0;
	}

	return 1;
}


static int get_last_msk_cb(void *ctx, int argc, char *argv[], char *col[])
{
	char **last_msk = ctx;

	if (argc < 1 || !argv[0])
		return 0;

	free(*last_msk);
	*last_msk = strdup(argv[0]);

	return 0;
}


static char * get_last_msk(struct sigma_dut *dut, sqlite3 *db,
			   const char *username)
{
	char *sql, *last_msk = NULL;

	sql = sqlite3_mprintf("SELECT last_msk FROM users WHERE identity=%Q",
			      username);
	if (!sql)
		return NULL;

	if (sqlite3_exec(db, sql, get_last_msk_cb, &last_msk, NULL) !=
	    SQLITE_OK) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"SQL operation to fetch last_msk failed: %s",
				sqlite3_errmsg(db));
		sqlite3_free(sql);
		return NULL;
	}

	sqlite3_free(sql);

	return last_msk;
}


static int aaa_auth_status(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd, const char *username,
			   int timeout)
{
	sqlite3 *db;
	char *sql = NULL;
	int i;
	char resp[500];

	if (sqlite3_open(SERVER_DB, &db)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open SQLite database %s",
				SERVER_DB);
		return -1;
	}

	sql = sqlite3_mprintf("UPDATE users SET last_msk=NULL WHERE identity=%Q",
			      username);
	if (!sql) {
		sqlite3_close(db);
		return -1;
	}

	if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"SQL operation to clear last_msk failed: %s",
				sqlite3_errmsg(db));
		sqlite3_free(sql);
		sqlite3_close(db);
		return -1;
	}

	sqlite3_free(sql);

	if (sqlite3_changes(db) < 1) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"No DB rows modified (specified user not found)");
		sqlite3_close(db);
		return -1;
	}

	snprintf(resp, sizeof(resp), "AuthStatus,TIMEOUT,MSK,NULL");

	for (i = 0; i < timeout; i++) {
		char *last_msk;

		last_msk = get_last_msk(dut, db, username);
		if (last_msk) {
			if (strcmp(last_msk, "FAIL") == 0) {
				snprintf(resp, sizeof(resp),
					 "AuthStatus,FAIL,MSK,NULL");
			} else {
				snprintf(resp, sizeof(resp),
					 "AuthStatus,SUCCESS,MSK,%s", last_msk);
			}
			free(last_msk);
			break;
		}
		sleep(1);
	}

	sqlite3_close(db);

	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static int get_last_serial_cb(void *ctx, int argc, char *argv[], char *col[])
{
	char **last_serial = ctx;

	if (argc < 1 || !argv[0])
		return 0;

	free(*last_serial);
	*last_serial = strdup(argv[0]);

	return 0;
}


static char * get_last_serial(struct sigma_dut *dut, sqlite3 *db,
			      const char *addr)
{
	char *sql, *last_serial = NULL;

	sql = sqlite3_mprintf("SELECT serialnum FROM cert_enroll WHERE mac_addr=%Q",
			      addr);
	if (!sql)
		return NULL;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "SQL: %s", sql);

	if (sqlite3_exec(db, sql, get_last_serial_cb, &last_serial, NULL) !=
	    SQLITE_OK) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"SQL operation to fetch last_serial failed: %s",
				sqlite3_errmsg(db));
		sqlite3_free(sql);
		return NULL;
	}

	sqlite3_free(sql);

	return last_serial;
}


static int osu_cert_enroll_status(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd, const char *addr,
				  int timeout)
{
	sqlite3 *db;
	int i;
	char resp[500];

	if (sqlite3_open(SERVER_DB, &db)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open SQLite database %s",
				SERVER_DB);
		return -1;
	}

	snprintf(resp, sizeof(resp), "OSUStatus,TIMEOUT");

	for (i = 0; i < timeout; i++) {
		char *last_serial;

		last_serial = get_last_serial(dut, db, addr);
		if (last_serial) {
			if (strcmp(last_serial, "FAIL") == 0) {
				snprintf(resp, sizeof(resp),
					 "OSUStatus,FAIL");
			} else if (strlen(last_serial) > 0) {
				snprintf(resp, sizeof(resp),
					 "OSUStatus,SUCCESS,SerialNo,%s",
					 last_serial);
			}
			free(last_serial);
			break;
		}
		sleep(1);
	}

	sqlite3_close(db);

	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static int get_user_field_cb(void *ctx, int argc, char *argv[], char *col[])
{
	char **val = ctx;

	if (argc < 1 || !argv[0])
		return 0;

	free(*val);
	*val = strdup(argv[0]);

	return 0;
}


static char * get_user_field_helper(struct sigma_dut *dut, sqlite3 *db,
				    const char *id_field,
				    const char *identity, const char *field)
{
	char *sql, *val = NULL;

	sql = sqlite3_mprintf("SELECT %s FROM users WHERE %s=%Q",
			      field, id_field, identity);
	if (!sql)
		return NULL;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "SQL: %s", sql);

	if (sqlite3_exec(db, sql, get_user_field_cb, &val, NULL) != SQLITE_OK) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"SQL operation to fetch user field failed: %s",
				sqlite3_errmsg(db));
		sqlite3_free(sql);
		return NULL;
	}

	sqlite3_free(sql);

	return val;
}


static char * get_user_field(struct sigma_dut *dut, sqlite3 *db,
			     const char *identity, const char *field)
{
	return get_user_field_helper(dut, db, "identity", identity, field);
}


static char * get_user_dmacc_field(struct sigma_dut *dut, sqlite3 *db,
				   const char *identity, const char *field)
{
	return get_user_field_helper(dut, db, "osu_user", identity, field);
}


static int osu_remediation_status(struct sigma_dut *dut,
				  struct sigma_conn *conn, int timeout,
				  const char *username, const char *serialno)
{
	sqlite3 *db;
	int i;
	char resp[500];
	char name[100];
	char *remediation = NULL;
	int dmacc = 0;

	if (!username && !serialno)
		return -1;
	if (!username) {
		snprintf(name, sizeof(name), "cert-%s", serialno);
		username = name;
	}

	if (sqlite3_open(SERVER_DB, &db)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open SQLite database %s",
				SERVER_DB);
		return -1;
	}

	remediation = get_user_field(dut, db, username, "remediation");
	if (!remediation) {
		remediation = get_user_dmacc_field(dut, db, username,
						   "remediation");
		dmacc = 1;
	}
	if (!remediation) {
		snprintf(resp, sizeof(resp),
			 "RemediationStatus,User entry not found");
		goto done;
	}
	if (remediation[0] == '\0') {
		snprintf(resp, sizeof(resp),
			 "RemediationStatus,User was not configured to need remediation");
		goto done;
	}

	snprintf(resp, sizeof(resp), "RemediationStatus,TIMEOUT");

	for (i = 0; i < timeout; i++) {
		sleep(1);
		free(remediation);
		if (dmacc)
			remediation = get_user_dmacc_field(dut, db, username,
							   "remediation");
		else
			remediation = get_user_field(dut, db, username,
						     "remediation");
		if (remediation && remediation[0] == '\0') {
			snprintf(resp, sizeof(resp),
				 "RemediationStatus,Remediation Complete");
			break;
		}
	}

done:
	free(remediation);
	sqlite3_close(db);

	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static int cmd_server_request_status(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd)
{
	const char *var, *username, *serialno, *imsi, *addr, *status;
	int osu, timeout;
	char resp[500];
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

	if (osu && status && strcasecmp(status, "Remediation") == 0)
		return osu_remediation_status(dut, conn, timeout, username,
					      serialno);

	if (!osu && status && strcasecmp(status, "Authentication") == 0 &&
	    username)
		return aaa_auth_status(dut, conn, cmd, username, timeout);

	if (!osu && status && strcasecmp(status, "Authentication") == 0 &&
	    serialno) {
		snprintf(resp, sizeof(resp), "cert-%s", serialno);
		return aaa_auth_status(dut, conn, cmd, resp, timeout);
	}

	if (osu && status && strcasecmp(status, "OSU") == 0 && addr)
		return osu_cert_enroll_status(dut, conn, cmd, addr, timeout);

	return 1;
}


static int osu_set_cert_reenroll(struct sigma_dut *dut, const char *serial,
				 int enable)
{
	sqlite3 *db;
	char *sql;
	char id[100];
	int ret = -1;

	if (sqlite3_open(SERVER_DB, &db)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open SQLite database %s",
				SERVER_DB);
		return -1;
	}

	snprintf(id, sizeof(id), "cert-%s", serial);
	sql = sqlite3_mprintf("UPDATE users SET remediation=%Q WHERE lower(identity)=lower(%Q)",
			      enable ? "machine" : "", id);
	if (!sql)
		goto fail;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "SQL: %s", sql);
	if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "SQL operation failed: %s",
				sqlite3_errmsg(db));
		goto fail;
	}

	if (sqlite3_changes(db) < 1) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "No DB rows modified (specified serial number not found)");
		goto fail;
	}

	ret = 0;
fail:
	sqlite3_close(db);

	return ret;
}


static int cmd_server_set_parameter(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *var, *root_ca, *inter_ca, *osu_cert, *issuing_arch, *name;
	const char *reenroll, *serial;
	int osu, timeout = -1;
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
	if (var)
		timeout = atoi(var);

	var = get_param(cmd, "ProvisioningProto");
	if (var && strcasecmp(var, "SOAP") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported ProvisioningProto");
		return 0;
	}

	reenroll = get_param(cmd, "CertReEnroll");
	serial = get_param(cmd, "SerialNo");
	if (reenroll && serial) {
		int enable;

		if (strcasecmp(reenroll, "Enable") == 0) {
			enable = 1;
		} else if (strcasecmp(reenroll, "Disable") == 0) {
			enable = 0;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid CertReEnroll value");
			return 0;
		}

		if (osu_set_cert_reenroll(dut, serial, enable) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to update certificate reenrollment state");
			return 0;
		}
	}

	name = get_param(cmd, "Name");
	root_ca = get_param(cmd, "TrustRootCACert");
	inter_ca = get_param(cmd, "InterCACert");
	osu_cert = get_param(cmd, "OSUServerCert");
	issuing_arch = get_param(cmd, "Issuing_Arch");

	if (timeout > -1) {
		/* TODO */
	}

	if (osu && name && root_ca && inter_ca && osu_cert && issuing_arch) {
		const char *srv;
		char buf[500];
		char buf2[500];
		int col;

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Update server certificate setup");

		if (strcasecmp(name, "ruckus") == 0) {
			srv = "RKS";
		} else if (strcasecmp(name, "aruba") == 0) {
			srv = "ARU";
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported Name value");
			return 0;
		}

		if (strcasecmp(issuing_arch, "col2") == 0) {
			col = 2;
		} else if (strcasecmp(issuing_arch, "col4") == 0) {
			col = 4;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported Issuing_Arch value");
			return 0;
		}

		if (strcasecmp(root_ca, "ID-T") == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"OSU trust root: NetworkFX");
			if (system("cp " CERT_DIR "/IDT-cert-RootCA.pem "
				   CERT_DIR "/cacert.pem") < 0)
				return -2;
		} else if (strcasecmp(root_ca, "ID-Y") == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"OSU trust root: NetworkFX");
			if (system("cp " CERT_DIR "/IDY-cert-RootCA.pem "
				   CERT_DIR "/cacert.pem") < 0)
				return -2;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported TrustRootCACert value");
			return 0;
		}

		if (strcasecmp(inter_ca, "ID-Z.2") == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"OSU intermediate CA: NetworkFX (col2)");
			if (system("cat " CERT_DIR "/IDZ2-cert-InterCA.pem >> "
				   CERT_DIR "/cacert.pem") < 0)
				return -2;
		} else if (strcasecmp(inter_ca, "ID-Z.4") == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"OSU intermediate CA: DigiCert (col2)");
			if (system("cat " CERT_DIR "/IDZ4-cert-InterCA.pem >> "
				   CERT_DIR "/cacert.pem") < 0)
				return -2;
		} else if (strcasecmp(inter_ca, "ID-Z.6") == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"OSU intermediate CA: NetworkFX (col4)");
			if (system("cat " CERT_DIR "/IDZ6-cert-InterCA.pem >> "
				   CERT_DIR "/cacert.pem") < 0)
				return -2;
		} else if (strcasecmp(inter_ca, "ID-Z.8") == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"OSU intermediate CA: DigiCert (col4)");
			if (system("cat " CERT_DIR "/IDZ8-cert-InterCA.pem >> "
				   CERT_DIR "/cacert.pem") < 0)
				return -2;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported InterCACert value");
			return 0;
		}

		if (strcasecmp(osu_cert, "ID-Q") == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"OSU server cert: NetworkFX col%d",
					col);
			snprintf(buf, sizeof(buf),
				 "cp " CERT_DIR "/IDQ-cert-c%d-%s.pem "
				 CERT_DIR "/server.pem",
				 col, srv);
			snprintf(buf2, sizeof(buf2),
				 "cp " CERT_DIR "/IDQ-key-%s.pem "
				 CERT_DIR "/server.key", srv);
		} else if (strcasecmp(osu_cert, "ID-W") == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"OSU server cert: DigiCert col%d",
					col);
			snprintf(buf, sizeof(buf),
				 "cp " CERT_DIR "/IDW-cert-c%d-%s.pem "
				 CERT_DIR "/server.pem",
				 col, srv);
			snprintf(buf2, sizeof(buf2),
				 "cp " CERT_DIR "/IDW-key-%s.pem "
				 CERT_DIR "/server.key", srv);
		} else if (strcasecmp(osu_cert, "ID-R.2") == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"OSU server cert: NetworkFX revoked col%d",
					col);
			snprintf(buf, sizeof(buf),
				 "cp " CERT_DIR "/IDR2-cert-c%d-%s.pem "
				 CERT_DIR "/server.pem",
				 col, srv);
			snprintf(buf2, sizeof(buf2),
				 "cp " CERT_DIR "/IDR2-key-%s.pem "
				 CERT_DIR "/server.key", srv);
		} else if (strcasecmp(osu_cert, "ID-R.4") == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"OSU server cert: DigiCert revoked col%d",
					col);
			snprintf(buf, sizeof(buf),
				 "cp " CERT_DIR "/IDR4-cert-c%d-%s.pem "
				 CERT_DIR "/server.pem",
				 col, srv);
			snprintf(buf2, sizeof(buf2),
				 "cp " CERT_DIR "/IDR4-key-%s.pem "
				 CERT_DIR "/server.key", srv);
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported OSUServerCert value");
			return 0;
		}

		if (system(buf) < 0 || system(buf2) < 0)
			return -2;

		if (system("service apache2 reload") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to restart Apache");
			return 0;
		}
	}

	/* TODO */
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
	sigma_dut_reg_cmd("server_set_parameter", NULL,
			  cmd_server_set_parameter);
}
