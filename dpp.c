/*
 * Sigma Control API DUT (station/AP/sniffer)
 * Copyright (c) 2017, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include "wpa_ctrl.h"
#include "wpa_helpers.h"


static int sigma_dut_is_ap(struct sigma_dut *dut)
{
	return dut->device_type == AP_unknown ||
		dut->device_type == AP_testbed ||
		dut->device_type == AP_dut;
}


static int dpp_hostapd_run(struct sigma_dut *dut)
{
	if (dut->hostapd_running)
		return 0;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"Starting hostapd in unconfigured state for DPP");
	snprintf(dut->ap_ssid, sizeof(dut->ap_ssid), "unconfigured");
	dut->ap_channel = 11;
	dut->ap_is_dual = 0;
	dut->ap_mode = AP_11ng;
	dut->ap_key_mgmt = AP_OPEN;
	dut->ap_cipher = AP_PLAIN;
	return cmd_ap_config_commit(dut, NULL, NULL) == 1 ? 0 : -1;
}


static const char * dpp_get_curve(struct sigma_cmd *cmd, const char *arg)
{
	const char *val = get_param(cmd, arg);

	if (!val)
		val = "P-256";
	else if (strcasecmp(val, "BP-256R1") == 0)
		val = "BP-256";
	else if (strcasecmp(val, "BP-384R1") == 0)
		val = "BP-384";
	else if (strcasecmp(val, "BP-512R1") == 0)
		val = "BP-512";

	return val;
}


static int dpp_get_local_bootstrap(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *curve = dpp_get_curve(cmd, "DPPCryptoIdentifier");
	const char *bs = get_param(cmd, "DPPBS");
	char *pos, mac[50], buf[100], resp[1000], hex[2000];
	const char *ifname = get_station_ifname();

	if (strcasecmp(bs, "QR") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported DPPBS");
		return 0;
	}

	if (sigma_dut_is_ap(dut)) {
		u8 bssid[ETH_ALEN];

		if (!dut->hostapd_ifname) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"hostapd ifname not specified (-j)");
			return -2;
		}
		ifname = dut->hostapd_ifname;
		if (get_hwaddr(dut->hostapd_ifname, bssid) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Could not get MAC address for %s",
					dut->hostapd_ifname);
			return -2;
		}
		snprintf(mac, sizeof(mac), "%02x%02x%02x%02x%02x%02x",
			 bssid[0], bssid[1], bssid[2],
			 bssid[3], bssid[4], bssid[5]);
	} else {
		if (get_wpa_status(ifname, "address", mac, sizeof(mac)) < 0)
			return -2;
	}

	pos = mac;
	while (*pos) {
		if (*pos == ':')
			memmove(pos, pos + 1, strlen(pos));
		else
			pos++;
	}

	snprintf(buf, sizeof(buf),
		 "DPP_BOOTSTRAP_GEN type=qrcode curve=%s chan=81/11 mac=%s",
		 curve, mac);

	if (sigma_dut_is_ap(dut) && dpp_hostapd_run(dut) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to start hostapd");
		return 0;
	}

	if (wpa_command_resp(ifname, buf, resp, sizeof(resp)) < 0)
		return -2;
	if (strncmp(resp, "FAIL", 4) == 0)
		return -2;
	dut->dpp_local_bootstrap = atoi(resp);
	snprintf(buf, sizeof(buf), "DPP_BOOTSTRAP_GET_URI %d",
		 atoi(resp));
	if (wpa_command_resp(ifname, buf, resp, sizeof(resp)) < 0)
		return -2;
	if (strncmp(resp, "FAIL", 4) == 0)
		return -2;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "URI: %s", resp);
	ascii2hexstr(resp, hex);
	snprintf(resp, sizeof(resp), "BootstrappingData,%s", hex);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static int dpp_set_peer_bootstrap(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *val = get_param(cmd, "DPPBootstrappingdata");
	char uri[1000], buf[1200];
	int res;
	const char *ifname = get_station_ifname();

	if (!val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing DPPBootstrappingdata");
		return 0;
	}

	res = parse_hexstr(val, (unsigned char *) uri, sizeof(uri));
	if (res < 0 || (size_t) res >= sizeof(uri))
		return -2;
	uri[res] = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "URI: %s", uri);

	snprintf(buf, sizeof(buf), "DPP_QR_CODE %s", uri);

	if (sigma_dut_is_ap(dut)) {
		if (!dut->hostapd_ifname) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"hostapd ifname not specified (-j)");
			return -2;
		}
		ifname = dut->hostapd_ifname;

		if (dpp_hostapd_run(dut) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to start hostapd");
			return 0;
		}
	}

	if (wpa_command_resp(ifname, buf, buf, sizeof(buf)) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to parse URI");
		return 0;
	}
	dut->dpp_peer_bootstrap = atoi(buf);

	return 1;
}


static int dpp_hostapd_conf_update(struct sigma_dut *dut,
				   struct sigma_conn *conn, const char *ifname,
				   struct wpa_ctrl *ctrl)
{
	int res;
	char buf[2000], buf2[2500], *pos, *pos2;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"Update hostapd configuration based on DPP Config Object");

	if (wpa_command(ifname, "SET wpa 2") < 0 ||
	    wpa_command(ifname, "SET wpa_key_mgmt DPP") < 0 ||
	    wpa_command(ifname, "SET rsn_pairwise CCMP") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP security parameters");
		goto out;
	}

	res = get_wpa_cli_event(dut, ctrl, "DPP-CONFOBJ-SSID",
				buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,No DPP-CONFOBJ-SSID");
		goto out;
	}
	pos = strchr(buf, ' ');
	if (!pos)
		return -2;
	pos++;
	sigma_dut_print(dut, DUT_MSG_INFO,
			"DPP: Config Object SSID: %s", pos);
	snprintf(buf2, sizeof(buf2), "SET ssid %s", pos);
	if (wpa_command(ifname, buf2) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP SSID");
		goto out;
	}

	res = get_wpa_cli_event(dut, ctrl, "DPP-CONNECTOR",
				buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,No DPP-CONNECTOR");
		goto out;
	}
	pos = strchr(buf, ' ');
	if (!pos)
		return -2;
	pos++;
	sigma_dut_print(dut, DUT_MSG_INFO, "DPP: Connector: %s", pos);
	snprintf(buf2, sizeof(buf2), "SET dpp_connector %s", pos);
	if (wpa_command(ifname, buf2) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP Connector");
		goto out;
	}

	res = get_wpa_cli_event(dut, ctrl, "DPP-C-SIGN-KEY",
				buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,No DPP-C-SIGN-KEY");
		goto out;
	}
	pos = strchr(buf, ' ');
	if (!pos)
		return -2;
	pos++;
	pos2 = strchr(pos, ' ');
	if (pos2)
		*pos2++ = '\0';
	sigma_dut_print(dut, DUT_MSG_INFO, "DPP: C-sign-key: %s", pos);
	snprintf(buf2, sizeof(buf2), "SET dpp_csign %s", pos);
	if (wpa_command(ifname, buf2) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP C-sign-key");
		goto out;
	}
	if (pos2) {
		sigma_dut_print(dut, DUT_MSG_INFO, "DPP: C-sign-key expiry: %s",
				pos2);
		snprintf(buf2, sizeof(buf2), "SET dpp_csign_expiry %s", pos2);
		if (wpa_command(ifname, buf2) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to update AP C-sign-key expiry");
			goto out;
		}
	}

	res = get_wpa_cli_event(dut, ctrl, "DPP-NET-ACCESS-KEY",
				buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,No DPP-NET-ACCESS-KEY");
		goto out;
	}
	pos = strchr(buf, ' ');
	if (!pos)
		return -2;
	pos++;
	pos2 = strchr(pos, ' ');
	if (pos2)
		*pos2++ = '\0';
	sigma_dut_print(dut, DUT_MSG_INFO, "DPP: netAccessKey: %s", pos);
	snprintf(buf2, sizeof(buf2), "SET dpp_netaccesskey %s", pos);
	if (wpa_command(ifname, buf2) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP netAccessKey");
		goto out;
	}
	if (pos2) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"DPP: netAccessKey expiry: %s", pos2);
		snprintf(buf2, sizeof(buf2), "SET dpp_netaccesskey_expiry %s",
			 pos2);
		if (wpa_command(ifname, buf2) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to update AP netAccessKey expiry");
			goto out;
		}
	}

	if (wpa_command(ifname, "DISABLE") < 0 ||
	    wpa_command(ifname, "ENABLE") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP configuration");
		goto out;
	}

	res = get_wpa_cli_event(dut, ctrl, "AP-ENABLED", buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,No AP-ENABLED");
		goto out;
	}

	return 1;
out:
	return 0;
}


static int dpp_manual_dpp(struct sigma_dut *dut,
			  struct sigma_conn *conn,
			  struct sigma_cmd *cmd)
{
	/* TODO */
	return -1;
}


static int dpp_automatic_dpp(struct sigma_dut *dut,
			     struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	const char *bs = get_param(cmd, "DPPBS");
	const char *auth_role = get_param(cmd, "DPPAuthRole");
	const char *prov_role = get_param(cmd, "DPPProvisioningRole");
	const char *pkex_code = get_param(cmd, "DPPPKEXCode");
	const char *pkex_code_id = get_param(cmd, "DPPPKEXCodeIdentifier");
	const char *wait_conn = get_param(cmd, "DPPWaitForConnect");
	const char *self_conf = get_param(cmd, "DPPSelfConfigure");
	const char *role;
	const char *val;
	const char *conf_role;
	int mutual;
	int conf_index = -1;
	char buf[2000];
	char conf_ssid[100];
	char conf_pass[100];
	char pkex_identifier[200];
	struct wpa_ctrl *ctrl;
	int res;
	unsigned int old_timeout;
	int own_pkex_id = -1;
	const char *ifname = get_station_ifname();
	const char *auth_events[] = {
		"DPP-AUTH-SUCCESS",
		"DPP-NOT-COMPATIBLE",
		"DPP-RESPONSE-PENDING",
		"DPP-SCAN-PEER-QR-CODE",
		NULL
	};
	const char *conf_events[] = {
		"DPP-CONF-RECEIVED",
		"DPP-CONF-SENT",
		"DPP-CONF-FAILED",
		NULL
	};
	const char *conn_events[] = {
		"PMKSA-CACHE-ADDED",
		"CTRL-EVENT-CONNECTED",
		NULL
	};

	if (!wait_conn)
		wait_conn = "no";
	if (!self_conf)
		self_conf = "no";

	if (!auth_role) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing DPPAuthRole");
		return 0;
	}

	if (!prov_role) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing DPPProvisioningRole");
		return 0;
	}

	val = get_param(cmd, "DPPAuthDirection");
	mutual = val && strcasecmp(val, "Mutual") == 0;

	if (sigma_dut_is_ap(dut)) {
		if (!dut->hostapd_ifname) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"hostapd ifname not specified (-j)");
			return -2;
		}
		ifname = dut->hostapd_ifname;

		if (dpp_hostapd_run(dut) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to start hostapd");
			return 0;
		}
	}

	if (strcasecmp(prov_role, "Configurator") == 0) {
		if (dut->dpp_conf_id < 0) {
			snprintf(buf, sizeof(buf),
				 "DPP_CONFIGURATOR_ADD curve=%s",
				 dpp_get_curve(cmd, "DPPSigningKeyECC"));
			if (wpa_command_resp(ifname, buf,
					     buf, sizeof(buf)) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to set up configurator");
				return 0;
			}
			dut->dpp_conf_id = atoi(buf);
		}
		role = "configurator";
	} else if (strcasecmp(prov_role, "Enrollee") == 0) {
		role = "enrollee";
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unknown DPPProvisioningRole");
		return 0;
	}

	pkex_identifier[0] = '\0';
	if (strcasecmp(bs, "PKEX") == 0) {
		if (!pkex_code) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Missing DPPPKEXCode");
			return 0;
		}

		if (pkex_code_id)
			snprintf(pkex_identifier, sizeof(pkex_identifier),
				 "identifier=%s ", pkex_code_id);

		snprintf(buf, sizeof(buf),
			 "DPP_BOOTSTRAP_GEN type=pkex curve=%s",
			 dpp_get_curve(cmd, "DPPCryptoIdentifier"));
		if (wpa_command_resp(ifname, buf, buf, sizeof(buf)) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set up PKEX");
			return 0;
		}
		own_pkex_id = atoi(buf);
	}

	ctrl = open_wpa_mon(ifname);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return -2;
	}

	old_timeout = dut->default_timeout;
	val = get_param(cmd, "DPPTimeout");
	if (val && atoi(val) > 0) {
		dut->default_timeout = atoi(val);
		sigma_dut_print(dut, DUT_MSG_DEBUG, "DPP timeout: %u",
				dut->default_timeout);
	}

	conf_ssid[0] = '\0';
	conf_pass[0] = '\0';
	val = get_param(cmd, "DPPConfIndex");
	if (val)
		conf_index = atoi(val);
	val = get_param(cmd, "DPPConfEnrolleeRole");
	switch (conf_index) {
	case 1:
		ascii2hexstr("DPPNET01", buf);
		snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (val && strcasecmp(val, "AP") == 0)
			conf_role = "ap-dpp";
		else
			conf_role = "sta-dpp";
		break;
	case 2:
		ascii2hexstr("DPPNET01", buf);
		snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		/* TODO: raw PSK */
		if (val && strcasecmp(val, "AP") == 0)
			conf_role = "ap-psk";
		else
			conf_role = "sta-psk";
		break;
	case 3:
		ascii2hexstr("DPPNET01", buf);
		snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		ascii2hexstr("ThisIsDppPassphrase", buf);
		snprintf(conf_pass, sizeof(conf_pass), "pass=%s", buf);
		if (val && strcasecmp(val, "AP") == 0)
			conf_role = "ap-psk";
		else
			conf_role = "sta-psk";
		break;
	}

	if (strcasecmp(auth_role, "Initiator") == 0) {
		char own_txt[20];

		if (mutual)
			snprintf(own_txt, sizeof(own_txt), " own=%d",
				 dut->dpp_local_bootstrap);
		else
			own_txt[0] = '\0';
		if (strcasecmp(bs, "QR") == 0 &&
		    strcasecmp(prov_role, "Configurator") == 0) {
			snprintf(buf, sizeof(buf),
				 "DPP_AUTH_INIT peer=%d%s role=%s conf=%s %s %s configurator=%d",
				 dut->dpp_peer_bootstrap, own_txt, role,
				 conf_role, conf_ssid, conf_pass,
				 dut->dpp_conf_id);
		} else if (strcasecmp(bs, "QR") == 0) {
			snprintf(buf, sizeof(buf),
				 "DPP_AUTH_INIT peer=%d%s role=%s",
				 dut->dpp_peer_bootstrap, own_txt, role);
		} else if (strcasecmp(bs, "PKEX") == 0 &&
			   strcasecmp(prov_role, "Configurator") == 0) {
			snprintf(buf, sizeof(buf),
				 "DPP_PKEX_ADD own=%d init=1 role=%s conf=%s %s %s configurator=%d %scode=%s",
				 own_pkex_id, role, conf_role,
				 conf_ssid, conf_pass, dut->dpp_conf_id,
				 pkex_identifier, pkex_code);
		} else if (strcasecmp(bs, "PKEX") == 0) {
			snprintf(buf, sizeof(buf),
				 "DPP_PKEX_ADD own=%d init=1 role=%s %scode=%s",
				 own_pkex_id, role, pkex_identifier, pkex_code);
		}
		if (wpa_command(ifname, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to initiate DPP authentication");
			goto out;
		}
	} else if (strcasecmp(auth_role, "Responder") == 0) {
		int freq = 2462;

		if (strcasecmp(prov_role, "Configurator") == 0) {
			snprintf(buf, sizeof(buf),
				 "SET dpp_configurator_params  conf=%s %s %s configurator=%d",
				 conf_role, conf_ssid, conf_pass,
				 dut->dpp_conf_id);
			if (wpa_command(ifname, buf) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to set configurator parameters");
				goto out;
			}
		}
		if (strcasecmp(bs, "PKEX") == 0) {
			freq = 2437;

			snprintf(buf, sizeof(buf),
				 "DPP_PKEX_ADD own=%d role=%s %scode=%s",
				 own_pkex_id, role, pkex_identifier, pkex_code);
			if (wpa_command(ifname, buf) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to configure DPP PKEX");
				goto out;
			}
		}

		if (!sigma_dut_is_ap(dut)) {
			snprintf(buf, sizeof(buf), "DPP_LISTEN %d role=%s",
				 freq, role);
			if (wpa_command(ifname, buf) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to start DPP listen");
				goto out;
			}
		}
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unknown DPPAuthRole");
		goto out;
	}

	res = get_wpa_cli_events(dut, ctrl, auth_events, buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "BootstrapResult,OK,AuthResult,Timeout");
		goto out;
	}
	sigma_dut_print(dut, DUT_MSG_DEBUG, "DPP auth result: %s", buf);

	if (strstr(buf, "DPP-NOT-COMPATIBLE")) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "BootstrapResult,OK,AuthResult,ROLES_NOT_COMPATIBLE");
		goto out;
	}

	if (!strstr(buf, "DPP-AUTH-SUCCESS")) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "BootstrapResult,OK,AuthResult,FAILED");
		goto out;
	}

	res = get_wpa_cli_events(dut, ctrl, conf_events, buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "BootstrapResult,OK,AuthResult,OK,ConfResult,Timeout");
		goto out;
	}
	sigma_dut_print(dut, DUT_MSG_DEBUG, "DPP conf result: %s", buf);

	if (!strstr(buf, "DPP-CONF-SENT") &&
	    !strstr(buf, "DPP-CONF-RECEIVED")) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "BootstrapResult,OK,AuthResult,OK,ConfResult,FAILED");
		goto out;
	}

	if (sigma_dut_is_ap(dut) &&
	    strcasecmp(prov_role, "Enrollee") == 0) {
		res = dpp_hostapd_conf_update(dut, conn, ifname, ctrl);
		if (res == 0)
			goto out;
		if (res < 0) {
			send_resp(dut, conn, SIGMA_ERROR, NULL);
			goto out;
		}
	}

	if (strcasecmp(wait_conn, "Yes") == 0 &&
	    !sigma_dut_is_ap(dut) &&
	    strcasecmp(prov_role, "Enrollee") == 0) {
		res = get_wpa_cli_events(dut, ctrl, conn_events,
					 buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,Timeout,NetworkConnectResult,Timeout");
			goto out;
		}
		sigma_dut_print(dut, DUT_MSG_DEBUG, "DPP connect result: %s",
				buf);

		if (strstr(buf, "PMKSA-CACHE-ADDED")) {
			res = get_wpa_cli_events(dut, ctrl, conn_events,
						 buf, sizeof(buf));
			if (res < 0) {
				send_resp(dut, conn, SIGMA_COMPLETE,
					  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,OK,NetworkConnectResult,Timeout");
				goto out;
			}
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"DPP connect result: %s", buf);
			if (strstr(buf, "CTRL-EVENT-CONNECTED"))
				send_resp(dut, conn, SIGMA_COMPLETE,
					  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,OK,NetworkConnectResult,OK");
			else
				send_resp(dut, conn, SIGMA_COMPLETE,
					  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,OK,NetworkConnectResult,Timeout");
			goto out;
		}

		send_resp(dut, conn, SIGMA_COMPLETE,
			  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkConnectResult,OK");
		goto out;
	}

	send_resp(dut, conn, SIGMA_COMPLETE,
		  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK");
out:
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);
	dut->default_timeout = old_timeout;
	return 0;
}


int dpp_dev_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			struct sigma_cmd *cmd)
{
	const char *type = get_param(cmd, "DPPActionType");
	const char *bs = get_param(cmd, "DPPBS");

	if (!bs) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing DPPBS");
		return 0;
	}

	if (!type) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing DPPActionType");
		return 0;
	}

	if (strcasecmp(type, "GetLocalBootstrap") == 0)
		return dpp_get_local_bootstrap(dut, conn, cmd);
	if (strcasecmp(type, "SetPeerBootstrap") == 0)
		return dpp_set_peer_bootstrap(dut, conn, cmd);
	if (strcasecmp(type, "ManualDPP") == 0)
		return dpp_manual_dpp(dut, conn, cmd);
	if (strcasecmp(type, "AutomaticDPP") == 0)
		return dpp_automatic_dpp(dut, conn, cmd);

	send_resp(dut, conn, SIGMA_ERROR,
		  "errorCode,Unsupported DPPActionType");
	return 0;
}
