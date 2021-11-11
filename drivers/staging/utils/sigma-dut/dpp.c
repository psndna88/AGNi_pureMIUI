/*
 * Sigma Control API DUT (station/AP/sniffer)
 * Copyright (c) 2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2020, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/wait.h>
#include "wpa_ctrl.h"
#include "wpa_helpers.h"

extern char *sigma_wpas_ctrl;
extern char *sigma_cert_path;

#ifdef ANDROID
char *dpp_qrcode_file = "/sdcard/wpadebug_qrdata.txt";
#endif /* ANDROID */


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
	if (!dut->ap_oper_chn)
		dut->ap_channel = 11;
	dut->ap_is_dual = 0;
	dut->ap_mode = dut->ap_channel <= 14 ? AP_11ng : AP_11na;
	dut->ap_key_mgmt = AP_OPEN;
	dut->ap_cipher = AP_PLAIN;
	if (!dut->ap_dpp_conf_addr || !dut->ap_dpp_conf_pkhash)
		dut->ap_start_disabled = 1;
	return cmd_ap_config_commit(dut, NULL, NULL) == 1 ? 0 : -1;
}


static int dpp_hostapd_beacon(struct sigma_dut *dut)
{
	const char *ifname = dut->hostapd_ifname;

	if (!dut->ap_start_disabled)
		return 0;

	sigma_dut_print(dut, DUT_MSG_INFO, "Start beaconing");
	if (!ifname ||
	    wpa_command(ifname, "SET start_disabled 0") < 0 ||
	    wpa_command(ifname, "DISABLE") < 0 ||
	    wpa_command(ifname, "ENABLE") < 0)
		return -1;

	dut->ap_start_disabled = 0;
	return 0;
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


static enum sigma_cmd_result
dpp_get_local_bootstrap(struct sigma_dut *dut, struct sigma_conn *conn,
			struct sigma_cmd *cmd, int send_result, int *success)
{
	const char *curve = dpp_get_curve(cmd, "DPPCryptoIdentifier");
	const char *bs = get_param(cmd, "DPPBS");
	const char *chan_list = get_param(cmd, "DPPChannelList");
	const char *tcp = get_param(cmd, "DPPOverTCP");
	char *pos, mac[50], buf[200], resp[1000], hex[2000];
	const char *ifname = get_station_ifname(dut);
	int res;
	const char *type;
	int include_mac;

	include_mac = !tcp || strcasecmp(tcp, "yes") != 0;

	if (success)
		*success = 0;
	if (strcasecmp(bs, "QR") == 0) {
		type = "qrcode";
	} else if (strcasecmp(bs, "NFC") == 0) {
		type ="nfc-uri";
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported DPPBS");
		return STATUS_SENT_ERROR;
	}

	if (sigma_dut_is_ap(dut)) {
		u8 bssid[ETH_ALEN];

		if (!dut->hostapd_ifname) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"hostapd ifname not specified (-j)");
			return ERROR_SEND_STATUS;
		}
		ifname = dut->hostapd_ifname;
		if (get_hwaddr(dut->hostapd_ifname, bssid) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Could not get MAC address for %s",
					dut->hostapd_ifname);
			return ERROR_SEND_STATUS;
		}
		snprintf(mac, sizeof(mac), "%02x%02x%02x%02x%02x%02x",
			 bssid[0], bssid[1], bssid[2],
			 bssid[3], bssid[4], bssid[5]);
	} else {
		if (get_wpa_status(ifname, "address", mac, sizeof(mac)) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to get own MAC address from wpa_supplicant");
			return STATUS_SENT_ERROR;
		}
	}

	pos = mac;
	while (*pos) {
		if (*pos == ':')
			memmove(pos, pos + 1, strlen(pos));
		else
			pos++;
	}

	if (sigma_dut_is_ap(dut) && dpp_hostapd_run(dut) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to start hostapd");
		return STATUS_SENT_ERROR;
	}

	if (chan_list &&
	    (strcmp(chan_list, "0/0") == 0 || chan_list[0] == '\0')) {
		/* No channel list */
		res = snprintf(buf, sizeof(buf),
			       "DPP_BOOTSTRAP_GEN type=%s curve=%s%s%s",
			       type, curve,
			       include_mac ? " mac=" : "",
			       include_mac ? mac : "");
	} else if (chan_list) {
		/* Channel list override (CTT case) - space separated tuple(s)
		 * of OperatingClass/Channel; convert to wpa_supplicant/hostapd
		 * format: comma separated tuples */
		strlcpy(resp, chan_list, sizeof(resp));
		for (pos = resp; *pos; pos++) {
			if (*pos == ' ')
				*pos = ',';
		}
		res = snprintf(buf, sizeof(buf),
			       "DPP_BOOTSTRAP_GEN type=%s curve=%s chan=%s%s%s",
			       type, curve, resp, include_mac ? " mac=" : "",
			       include_mac ? mac : "");
	} else {
		int channel = 11;

		/* Default channel list (normal DUT case) */
		if (sigma_dut_is_ap(dut) && dut->hostapd_running &&
		    dut->ap_oper_chn &&
		    dut->ap_channel > 0 && dut->ap_channel <= 13)
			channel = dut->ap_channel;
		res = snprintf(buf, sizeof(buf),
			       "DPP_BOOTSTRAP_GEN type=%s curve=%s chan=81/%d%s%s",
			       type, curve, channel, include_mac ? " mac=" : "",
			       include_mac ? mac : "");
	}

	if (res < 0 || res >= sizeof(buf) ||
	    wpa_command_resp(ifname, buf, resp, sizeof(resp)) < 0 ||
	    strncmp(resp, "FAIL", 4) == 0)
		return ERROR_SEND_STATUS;
	dut->dpp_local_bootstrap = atoi(resp);
	snprintf(buf, sizeof(buf), "DPP_BOOTSTRAP_GET_URI %d",
		 atoi(resp));
	if (wpa_command_resp(ifname, buf, resp, sizeof(resp)) < 0 ||
	    strncmp(resp, "FAIL", 4) == 0)
		return ERROR_SEND_STATUS;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "URI: %s", resp);

	if (send_result) {
		ascii2hexstr(resp, hex);
		res = snprintf(resp, sizeof(resp), "BootstrappingData,%s", hex);
		send_resp(dut, conn, SIGMA_COMPLETE,
			  res >= 0 && res < sizeof(resp) ? resp : NULL);
	}

	if (success)
		*success = 1;
	return STATUS_SENT;
}


static enum sigma_cmd_result dpp_set_peer_bootstrap(struct sigma_dut *dut,
						    struct sigma_conn *conn,
						    struct sigma_cmd *cmd)
{
	const char *val = get_param(cmd, "DPPBootstrappingdata");
	char uri[1000];
	int res;

	if (!val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing DPPBootstrappingdata");
		return STATUS_SENT_ERROR;
	}

	res = parse_hexstr(val, (unsigned char *) uri, sizeof(uri));
	if (res < 0 || (size_t) res >= sizeof(uri))
		return ERROR_SEND_STATUS;
	uri[res] = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "URI: %s", uri);
	free(dut->dpp_peer_uri);
	dut->dpp_peer_uri = strdup(uri);

	return SUCCESS_SEND_STATUS;
}


static int dpp_hostapd_conf_update(struct sigma_dut *dut,
				   struct sigma_conn *conn, const char *ifname,
				   struct wpa_ctrl *ctrl)
{
	int res;
	char buf[2000], buf2[2500], *pos, *pos2;
	const char *conf_data_events[] = {
		"DPP-CONNECTOR",
		"DPP-CONFOBJ-PASS",
		"DPP-CONFOBJ-PSK",
		"DPP-C-SIGN-KEY",
		"DPP-NET-ACCESS-KEY",
		NULL
	};
	unsigned int old_timeout;
	int legacy_akm, dpp_akm;
	char *connector = NULL, *psk = NULL, *csign = NULL,
		*net_access_key = NULL;
	char pass[64];
	int pass_len = 0;
	int ret = 0;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"Update hostapd configuration based on DPP Config Object");

	if (wpa_command(ifname, "SET wpa 2") < 0 ||
	    wpa_command(ifname, "SET wpa_key_mgmt DPP") < 0 ||
	    wpa_command(ifname, "SET ieee80211w 1") < 0 ||
	    wpa_command(ifname, "SET rsn_pairwise CCMP") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP security parameters");
		goto out;
	}

	res = get_wpa_cli_event(dut, ctrl, "DPP-CONFOBJ-AKM", buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,No DPP-CONFOBJ-AKM");
		goto out;
	}
	pos = strchr(buf, ' ');
	if (!pos)
		return -2;
	pos++;
	sigma_dut_print(dut, DUT_MSG_INFO,
			"DPP: Config Object AKM: %s", pos);
	legacy_akm = strstr(pos, "psk") != NULL || strstr(pos, "sae") != NULL;
	dpp_akm = strstr(pos, "dpp") != NULL;

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

	if (wpa_command(ifname, "SET utf8_ssid 1") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP UTF-8 SSID capa");
		goto out;
	}

	while ((dpp_akm && (!connector || !csign || !net_access_key)) ||
	       (legacy_akm && !pass_len && !psk)) {
		res = get_wpa_cli_events(dut, ctrl, conf_data_events,
					 buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Not all config object information received");
			goto out;
		}

		if (strstr(buf, "DPP-CONNECTOR")) {
			pos = strchr(buf, ' ');
			if (!pos) {
				ret = -2;
				goto out;
			}
			pos++;
			sigma_dut_print(dut, DUT_MSG_INFO, "DPP: Connector: %s",
					pos);
			if (!connector)
				connector = strdup(pos);
		} else if (strstr(buf, "DPP-C-SIGN-KEY")) {
			pos = strchr(buf, ' ');
			if (!pos) {
				ret = -2;
				goto out;
			}
			pos++;
			sigma_dut_print(dut, DUT_MSG_INFO,
					"DPP: C-sign-key: %s", pos);
			if (!csign)
				csign = strdup(pos);
		} else if (strstr(buf, "DPP-NET-ACCESS-KEY")) {
			pos = strchr(buf, ' ');
			if (!pos) {
				ret = -2;
				goto out;
			}
			pos++;
			if (!net_access_key)
				net_access_key = strdup(pos);
		} else if (strstr(buf, "DPP-CONFOBJ-PASS")) {
			pos = strchr(buf, ' ');
			if (!pos) {
				ret = -2;
				goto out;
			}
			pos++;
			pass_len = parse_hexstr(pos, (u8 *) pass, sizeof(pass));
			if (pass_len < 0 || (size_t) pass_len >= sizeof(pass)) {
				ret = -2;
				goto out;
			}
			pass[pass_len] = '\0';
			sigma_dut_print(dut, DUT_MSG_INFO,
					"DPP: Passphrase: %s", pass);
		} else if (strstr(buf, "DPP-CONFOBJ-PSK")) {
			pos = strchr(buf, ' ');
			if (!pos) {
				ret = -2;
				goto out;
			}
			pos++;
			sigma_dut_print(dut, DUT_MSG_INFO, "DPP: PSK: %s", pos);
			if (!psk)
				psk = strdup(pos);
		}
	}

	if ((!connector || !dpp_akm) &&
	    wpa_command(ifname, "SET wpa_key_mgmt WPA-PSK") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP security parameters");
		goto out;
	}

	if (connector && dpp_akm && legacy_akm &&
	    wpa_command(ifname, "SET wpa_key_mgmt DPP WPA-PSK") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP security parameters");
		goto out;
	}

	if (pass_len) {
		snprintf(buf2, sizeof(buf2), "SET wpa_passphrase %s",
			 pass);
		if (wpa_command(ifname, buf2) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set passphrase");
			goto out;
		}
	} else if (psk) {
		snprintf(buf2, sizeof(buf2), "SET wpa_psk %s", psk);
		if (wpa_command(ifname, buf2) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set PSK");
			goto out;
		}
	}

	if (connector) {
		snprintf(buf2, sizeof(buf2), "SET dpp_connector %s", connector);
		if (wpa_command(ifname, buf2) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to update AP Connector");
			goto out;
		}
	}

	if (csign) {
		snprintf(buf2, sizeof(buf2), "SET dpp_csign %s", csign);
		if (wpa_command(ifname, buf2) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to update AP C-sign-key");
			goto out;
		}
	}

	if (net_access_key) {
		pos2 = strchr(net_access_key, ' ');
		if (pos2)
			*pos2++ = '\0';
		sigma_dut_print(dut, DUT_MSG_INFO, "DPP: netAccessKey: %s",
				net_access_key);
		snprintf(buf2, sizeof(buf2), "SET dpp_netaccesskey %s",
			 net_access_key);
		if (wpa_command(ifname, buf2) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to update AP netAccessKey");
			goto out;
		}
		if (pos2) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"DPP: netAccessKey expiry: %s", pos2);
			snprintf(buf2, sizeof(buf2),
				 "SET dpp_netaccesskey_expiry %s", pos2);
			if (wpa_command(ifname, buf2) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to update AP netAccessKey expiry");
				goto out;
			}
		}
	}

	if (dut->ap_start_disabled)
		sigma_dut_print(dut, DUT_MSG_INFO, "Clear ap_start_disabled");
	if (wpa_command(ifname, "SET start_disabled 0") < 0 &&
	    dut->ap_start_disabled) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to update AP security parameters");
		goto out;
	}
	dut->ap_start_disabled = 0;

	/* Wait for a possible Configuration Result to be sent */
	old_timeout = dut->default_timeout;
	dut->default_timeout = 1;
	get_wpa_cli_event(dut, ctrl, "DPP-TX-STATUS", buf, sizeof(buf));
	dut->default_timeout = old_timeout;

	if (dut->ap_oper_chn) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Set AP operating channel %d",
				dut->ap_channel);
		snprintf(buf, sizeof(buf), "SET channel %d", dut->ap_channel);
		wpa_command(ifname, buf);
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

	ret = 1;
out:
	free(connector);
	free(psk);
	free(csign);
	free(net_access_key);
	return ret;
}


struct dpp_test_info {
	const char *step;
	const char *frame;
	const char *attr;
	int value;
};

static const struct dpp_test_info dpp_tests[] = {
	{ "InvalidValue", "AuthenticationRequest", "WrappedData", 1 },
	{ "InvalidValue", "AuthenticationResponse", "WrappedData", 2 },
	{ "InvalidValue", "AuthenticationResponse", "PrimaryWrappedData", 2 },
	{ "InvalidValue", "AuthenticationConfirm", "WrappedData", 3 },
	{ "InvalidValue", "PKEXCRRequest", "WrappedData", 4 },
	{ "InvalidValue", "PKEXCRResponse", "WrappedData", 5 },
	{ "InvalidValue", "ConfigurationRequest", "WrappedData", 6 },
	{ "InvalidValue", "ConfigurationResponse", "WrappedData", 7 },
	{ "InvalidValue", "AuthenticationRequest", "InitCapabilities", 8 },
	{ "MissingAttribute", "AuthenticationRequest", "RespBSKeyHash", 10 },
	{ "MissingAttribute", "AuthenticationRequest", "InitBSKeyHash", 11 },
	{ "MissingAttribute", "AuthenticationRequest", "InitProtocolKey", 12 },
	{ "MissingAttribute", "AuthenticationRequest", "InitNonce", 13 },
	{ "MissingAttribute", "AuthenticationRequest", "InitCapabilities", 14 },
	{ "MissingAttribute", "AuthenticationRequest", "WrappedData", 15 },
	{ "MissingAttribute", "AuthenticationResponse", "DPPStatus", 16 },
	{ "MissingAttribute", "AuthenticationResponse", "RespBSKeyHash", 17 },
	{ "MissingAttribute", "AuthenticationResponse", "InitBSKeyHash", 18 },
	{ "MissingAttribute", "AuthenticationResponse", "RespProtocolKey", 19 },
	{ "MissingAttribute", "AuthenticationResponse", "RespNonce", 20 },
	{ "MissingAttribute", "AuthenticationResponse", "InitNonce", 21 },
	{ "MissingAttribute", "AuthenticationResponse", "RespCapabilities",
	  22 },
	{ "MissingAttribute", "AuthenticationResponse", "RespAuthTag", 23 },
	{ "MissingAttribute", "AuthenticationResponse", "WrappedData", 24 },
	{ "MissingAttribute", "AuthenticationResponse", "PrimaryWrappedData",
	  24 },
	{ "MissingAttribute", "AuthenticationConfirm", "DPPStatus", 25 },
	{ "MissingAttribute", "AuthenticationConfirm", "RespBSKeyHash", 26 },
	{ "MissingAttribute", "AuthenticationConfirm", "InitBSKeyHash", 27 },
	{ "MissingAttribute", "AuthenticationConfirm", "InitAuthTag", 28 },
	{ "MissingAttribute", "AuthenticationConfirm", "WrappedData", 29 },
	{ "InvalidValue", "AuthenticationResponse", "InitNonce", 30 },
	{ "InvalidValue", "AuthenticationResponse", "RespCapabilities", 31 },
	{ "InvalidValue", "AuthenticationResponse", "RespAuthTag", 32 },
	{ "InvalidValue", "AuthenticationConfirm", "InitAuthTag", 33 },
	{ "MissingAttribute", "PKEXExchangeRequest", "FiniteCyclicGroup", 34 },
	{ "MissingAttribute", "PKEXExchangeRequest", "EncryptedKey", 35 },
	{ "MissingAttribute", "PKEXExchangeResponse", "DPPStatus", 36 },
	{ "MissingAttribute", "PKEXExchangeResponse", "EncryptedKey", 37 },
	{ "MissingAttribute", "PKEXCRRequest", "BSKey", 38 },
	{ "MissingAttribute", "PKEXCRRequest", "InitAuthTag", 39 },
	{ "MissingAttribute", "PKEXCRRequest", "WrappedData", 40 },
	{ "MissingAttribute", "PKEXCRResponse", "BSKey", 41 },
	{ "MissingAttribute", "PKEXCRResponse", "RespAuthTag", 42 },
	{ "MissingAttribute", "PKEXCRResponse", "WrappedData", 43 },
	{ "InvalidValue", "PKEXExchangeRequest", "EncryptedKey", 44 },
	{ "InvalidValue", "PKEXExchangeResponse", "EncryptedKey", 45 },
	{ "InvalidValue", "PKEXExchangeResponse", "DPPStatus", 46 },
	{ "InvalidValue", "PKEXCRRequest", "BSKey", 47 },
	{ "InvalidValue", "PKEXCRResponse", "BSKey", 48 },
	{ "InvalidValue", "PKEXCRRequest", "InitAuthTag", 49 },
	{ "InvalidValue", "PKEXCRResponse", "RespAuthTag", 50 },
	{ "MissingAttribute", "ConfigurationRequest", "EnrolleeNonce", 51 },
	{ "MissingAttribute", "ConfigurationRequest", "ConfigAttr", 52 },
	{ "MissingAttribute", "ConfigurationRequest", "WrappedData", 53 },
	{ "MissingAttribute", "ConfigurationResponse", "EnrolleeNonce", 54 },
	{ "MissingAttribute", "ConfigurationResponse", "ConfigObj", 55 },
	{ "MissingAttribute", "ConfigurationResponse", "DPPStatus", 56 },
	{ "MissingAttribute", "ConfigurationResponse", "WrappedData", 57 },
	{ "InvalidValue", "ConfigurationResponse", "DPPStatus", 58 },
	{ "InvalidValue", "ConfigurationResponse", "EnrolleeNonce", 59 },
	{ "MissingAttribute", "PeerDiscoveryRequest", "TransactionID", 60 },
	{ "MissingAttribute", "PeerDiscoveryRequest", "Connector", 61 },
	{ "MissingAttribute", "PeerDiscoveryResponse", "TransactionID", 62 },
	{ "MissingAttribute", "PeerDiscoveryResponse", "DPPStatus", 63 },
	{ "MissingAttribute", "PeerDiscoveryResponse", "Connector", 64 },
	{ "InvalidValue", "AuthenticationRequest", "InitProtocolKey", 66 },
	{ "InvalidValue", "AuthenticationResponse", "RespProtocolKey", 67 },
	{ "InvalidValue", "AuthenticationRequest", "RespBSKeyHash", 68 },
	{ "InvalidValue", "AuthenticationRequest", "InitBSKeyHash", 69 },
	{ "InvalidValue", "AuthenticationResponse", "RespBSKeyHash", 70 },
	{ "InvalidValue", "AuthenticationResponse", "InitBSKeyHash", 71 },
	{ "InvalidValue", "AuthenticationConfirm", "RespBSKeyHash", 72 },
	{ "InvalidValue", "AuthenticationConfirm", "InitBSKeyHash", 73 },
	{ "InvalidValue", "AuthenticationResponse", "DPPStatus", 74 },
	{ "InvalidValue", "AuthenticationConfirm", "DPPStatus", 75 },
	{ "InvalidValue", "ConfigurationRequest", "ConfigAttr", 76 },
	{ "InvalidValue", "PeerDiscoveryResponse", "TransactionID", 77 },
	{ "InvalidValue", "PeerDiscoveryResponse", "DPPStatus", 78 },
	{ "InvalidValue", "PeerDiscoveryResponse", "Connector", 79 },
	{ "InvalidValue", "PeerDiscoveryRequest", "Connector", 80 },
	{ "InvalidValue", "AuthenticationRequest", "InitNonce", 81 },
	{ "InvalidValue", "PeerDiscoveryRequest", "TransactionID", 82 },
	{ "InvalidValue", "ConfigurationRequest", "EnrolleeNonce", 83 },
	{ "Timeout", "PKEXExchangeResponse", NULL, 84 },
	{ "Timeout", "PKEXCRRequest", NULL, 85 },
	{ "Timeout", "PKEXCRResponse", NULL, 86 },
	{ "Timeout", "AuthenticationRequest", NULL, 87 },
	{ "Timeout", "AuthenticationResponse", NULL, 88 },
	{ "Timeout", "AuthenticationConfirm", NULL, 89 },
	{ "Timeout", "ConfigurationRequest", NULL, 90 },
	{ NULL, NULL, NULL, 0 }
};


static int dpp_get_test(const char *step, const char *frame, const char *attr)
{
	int i;

	for (i = 0; dpp_tests[i].step; i++) {
		if (strcasecmp(step, dpp_tests[i].step) == 0 &&
		    strcasecmp(frame, dpp_tests[i].frame) == 0 &&
		    ((!attr && dpp_tests[i].attr == NULL) ||
		     (attr && strcasecmp(attr, dpp_tests[i].attr) == 0)))
			return dpp_tests[i].value;
	}

	return -1;
}


static int dpp_wait_tx(struct sigma_dut *dut, struct wpa_ctrl *ctrl,
		       int frame_type)
{
	char buf[200], tmp[20];
	int res;

	snprintf(tmp, sizeof(tmp), "type=%d", frame_type);
	for (;;) {
		res = get_wpa_cli_event(dut, ctrl, "DPP-TX", buf, sizeof(buf));
		if (res < 0)
			return -1;
		if (strstr(buf, tmp) != NULL)
			break;
	}

	return 0;
}


static int dpp_wait_tx_status(struct sigma_dut *dut, struct wpa_ctrl *ctrl,
			      int frame_type)
{
	char buf[200], tmp[20];
	int res;

	snprintf(tmp, sizeof(tmp), "type=%d", frame_type);
	for (;;) {
		res = get_wpa_cli_event(dut, ctrl, "DPP-TX", buf, sizeof(buf));
		if (res < 0)
			return -1;
		if (strstr(buf, tmp) != NULL)
			break;
	}

	res = get_wpa_cli_event(dut, ctrl, "DPP-TX-STATUS",
				buf, sizeof(buf));
	if (res < 0 || strstr(buf, "result=FAILED") != NULL)
		return -1;

	return 0;
}


static int dpp_wait_rx(struct sigma_dut *dut, struct wpa_ctrl *ctrl,
		       int frame_type, unsigned int max_wait)
{
	char buf[200], tmp[20];
	int res;
	unsigned int old_timeout;

	old_timeout = dut->default_timeout;
	if (max_wait > 0 && dut->default_timeout > max_wait)
		dut->default_timeout = max_wait;

	snprintf(tmp, sizeof(tmp), "type=%d", frame_type);
	for (;;) {
		res = get_wpa_cli_event(dut, ctrl, "DPP-RX", buf, sizeof(buf));
		if (res < 0) {
			dut->default_timeout = old_timeout;
			return -1;
		}
		if (strstr(buf, tmp) != NULL)
			break;
	}

	dut->default_timeout = old_timeout;
	return 0;
}


static int dpp_wait_rx_conf_req(struct sigma_dut *dut, struct wpa_ctrl *ctrl,
				unsigned int max_wait)
{
	char buf[200];
	int res;
	unsigned int old_timeout;

	old_timeout = dut->default_timeout;
	if (max_wait > 0 && dut->default_timeout > max_wait)
		dut->default_timeout = max_wait;

	for (;;) {
		res = get_wpa_cli_event(dut, ctrl, "DPP-CONF-REQ-RX",
					buf, sizeof(buf));
		if (res < 0) {
			dut->default_timeout = old_timeout;
			return -1;
		}

		break;
	}

	dut->default_timeout = old_timeout;
	return 0;
}


static int dpp_scan_peer_qrcode(struct sigma_dut *dut)
{
#ifdef ANDROID
	char buf[100];
	char *buf2 = NULL;
	FILE *fp = NULL;
	uint32_t length;
	unsigned int count;

	unlink(dpp_qrcode_file);

	snprintf(buf, sizeof(buf),
		 "am start -n w1.fi.wpadebug/w1.fi.wpadebug.QrCodeReadActivity");
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to launch QR Code scanner");
		return -1;
	}

	count = 0;
	while (!(fp = fopen(dpp_qrcode_file, "r"))) {
		if (count > dut->default_timeout) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to open dpp_qrcode_file - QR Code scanning timed out");
			return -1;
		}

		sleep(1);
		count++;
	}

	if (fseek(fp, 0, SEEK_END) < 0 || (length = ftell(fp)) <= 0 ||
	    fseek(fp, 0, SEEK_SET) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to get QR Code result file length");
		fclose(fp);
		return -1;
	}

	buf2 = malloc(length + 1);
	if (!buf2) {
		fclose(fp);
		return -1;
	}

	if (fread(buf2, 1, length, fp) != length) {
		fclose(fp);
		free(buf2);
		return -1;
	}

	fclose(fp);
	buf2[length] = '\0';

	free(dut->dpp_peer_uri);
	dut->dpp_peer_uri = strdup(buf2);
	free(buf2);
	return 0;
#else /* ANDROID */
	pid_t pid;
	int pid_status;
	int pipe_out[2];
	char buf[4000], *pos;
	ssize_t len;
	int res = -1, ret;
	struct timeval tv;
	fd_set rfd;

	if (pipe(pipe_out) != 0) {
		perror("pipe");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		perror("fork");
		close(pipe_out[0]);
		close(pipe_out[1]);
		return -1;
	}

	if (pid == 0) {
		char *argv[4] = { "zbarcam", "--raw", "--prescale=320x240",
				  NULL };

		dup2(pipe_out[1], STDOUT_FILENO);
		close(pipe_out[0]);
		close(pipe_out[1]);
		execv("/usr/bin/zbarcam", argv);
		perror("execv");
		exit(0);
		return -1;
	}

	close(pipe_out[1]);

	FD_ZERO(&rfd);
	FD_SET(pipe_out[0], &rfd);
	tv.tv_sec = dut->default_timeout;
	tv.tv_usec = 0;

	ret = select(pipe_out[0] + 1, &rfd, NULL, NULL, &tv);
	if (ret < 0) {
		perror("select");
		goto out;
	}
	if (ret == 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"QR Code scanning timed out");
		goto out;
	}

	len = read(pipe_out[0], buf, sizeof(buf));
	if (len <= 0)
		goto out;
	if (len == sizeof(buf))
		len--;
	buf[len] = '\0';
	pos = strchr(buf, '\n');
	if (pos)
		*pos = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "URI from QR scanner: %s", buf);

	free(dut->dpp_peer_uri);
	dut->dpp_peer_uri = strdup(buf);
	res = 0;
out:
	close(pipe_out[0]);
	kill(pid, SIGTERM);
	waitpid(pid, &pid_status, 0);

	return res;
#endif /* ANDROID */
}


static int dpp_display_own_qrcode(struct sigma_dut *dut)
{
	char buf[200], resp[2000];
	const char *ifname = get_station_ifname(dut);
#ifdef ANDROID
	FILE *fp;
#else /* ANDROID */
	pid_t pid;
	int pid_status;
#endif /* ANDROID */

	snprintf(buf, sizeof(buf), "DPP_BOOTSTRAP_GET_URI %d",
		 dut->dpp_local_bootstrap);
	if (wpa_command_resp(ifname, buf, resp, sizeof(resp)) < 0 ||
	    strncmp(resp, "FAIL", 4) == 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Own bootstrap URI: %s", resp);

#ifdef ANDROID
	unlink(dpp_qrcode_file);

	fp = fopen(dpp_qrcode_file, "w");
	if (!fp) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open file %s",
				dpp_qrcode_file);
		return -2;
	}

	fwrite(resp, 1, strlen(resp), fp);
	fclose(fp);

	snprintf(buf, sizeof(buf),
		 "am start -n w1.fi.wpadebug/w1.fi.wpadebug.QrCodeDisplayActivity");
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to display QR Code");
		return -1;
	}
#else /* ANDROID */
	pid = fork();
	if (pid < 0) {
		perror("fork");
		return -1;
	}

	if (pid == 0) {
		char *argv[3] = { "qr", resp, NULL };

		execv("/usr/bin/qr", argv);
		perror("execv");
		exit(0);
		return -1;
	}

	waitpid(pid, &pid_status, 0);
#endif /* ANDROID */

	return 0;
}


static int dpp_process_auth_response(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct wpa_ctrl *ctrl,
				     const char **auth_events,
				     const char *action_type,
				     int check_mutual, char *buf, size_t buflen)
{
	int res;

	res = get_wpa_cli_events(dut, ctrl, auth_events, buf, buflen);
	if (res < 0) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "BootstrapResult,OK,AuthResult,Timeout");
		return res;
	}
	sigma_dut_print(dut, DUT_MSG_DEBUG, "DPP auth result: %s", buf);

	if (strstr(buf, "DPP-RESPONSE-PENDING")) {
		/* Display own QR code in manual mode */
		if (action_type && strcasecmp(action_type, "ManualDPP") == 0 &&
		    dpp_display_own_qrcode(dut) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to display own QR code");
			return -1;
		}

		/* Wait for the actual result after the peer has scanned the
		 * QR Code. */
		res = get_wpa_cli_events(dut, ctrl, auth_events,
					 buf, buflen);
		if (res < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "BootstrapResult,OK,AuthResult,Timeout");
			return res;
		}

		sigma_dut_print(dut, DUT_MSG_DEBUG, "DPP auth result: %s", buf);
	} else if (strstr(buf, "DPP-AUTH-INIT-FAILED")) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "BootstrapResult,OK,AuthResult,Timeout");
		return -1;
	}

	if (check_mutual) {
		if (strstr(buf, "DPP-NOT-COMPATIBLE")) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "BootstrapResult,OK,AuthResult,ROLES_NOT_COMPATIBLE");
			return -1;
		}

		if (!strstr(buf, "DPP-AUTH-DIRECTION")) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,No event for auth direction seen");
			return -1;
		}

		sigma_dut_print(dut, DUT_MSG_DEBUG, "DPP auth direction: %s",
				buf);
		if (strstr(buf, "mutual=1") == NULL) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Peer did not use mutual authentication");
			return -1;
		}
	}

	return 0;
}


static int dpp_process_csr(struct sigma_dut *dut, const char *ifname,
			   char *csr_event)
{
	FILE *f;
	char buf[2000], cmd[2500], tmp[300];
	char *pos;
	int peer;
	size_t len;

	pos = strstr(csr_event, " peer=");
	if (!pos) {
		sigma_dut_print(dut, DUT_MSG_INFO, "No peer id known for CSR");
		return -1;
	}
	pos += 6;
	peer = atoi(pos);

	pos = strstr(csr_event, " csr=");
	if (!pos) {
		sigma_dut_print(dut, DUT_MSG_INFO, "No CSR found");
		return -1;
	}
	pos += 5;

	snprintf(tmp, sizeof(tmp), "%s/dpp-ca-certbag", sigma_cert_path);
	unlink(tmp);

	snprintf(tmp, sizeof(tmp), "%s/dpp-ca-csr", sigma_cert_path);
	f = fopen(tmp, "w");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to write CSR file");
		return -1;
	}
	fprintf(f, "%s", pos);
	fclose(f);

	if (run_system_wrapper(dut, "./dpp-ca.py %s", sigma_cert_path) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to run dpp-ca.py");
		return -1;
	}

	snprintf(tmp, sizeof(tmp), "%s/dpp-ca-certbag", sigma_cert_path);
	f = fopen(tmp, "r");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_INFO, "No certBag available");
		return -1;
	}
	len = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (len >= sizeof(buf)) {
		sigma_dut_print(dut, DUT_MSG_INFO, "No bufferroom for certBag");
		return -1;
	}
	buf[len] = '\0';

	snprintf(cmd, sizeof(cmd), "DPP_CA_SET peer=%d name=certBag value=%s",
		 peer, buf);
	if (wpa_command(ifname, cmd) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "DPP_CA_SET failed");
		return -1;
	}

	return 0;
}


static enum sigma_cmd_result dpp_automatic_dpp(struct sigma_dut *dut,
					       struct sigma_conn *conn,
					       struct sigma_cmd *cmd)
{
	const char *bs = get_param(cmd, "DPPBS");
	const char *type = get_param(cmd, "DPPActionType");
	const char *auth_role = get_param(cmd, "DPPAuthRole");
	const char *prov_role = get_param(cmd, "DPPProvisioningRole");
	const char *pkex_code = get_param(cmd, "DPPPKEXCode");
	const char *pkex_code_id = get_param(cmd, "DPPPKEXCodeIdentifier");
	const char *wait_conn = get_param(cmd, "DPPWaitForConnect");
	const char *self_conf = get_param(cmd, "DPPSelfConfigure");
	const char *step = get_param(cmd, "DPPStep");
	const char *frametype = get_param(cmd, "DPPFrameType");
	const char *attr = get_param(cmd, "DPPIEAttribute");
	const char *action_type = get_param(cmd, "DPPActionType");
	const char *tcp = get_param(cmd, "DPPOverTCP");
	const char *nfc_handover = get_param(cmd, "DPPNFCHandover");
	const char *role;
	const char *netrole = NULL;
	const char *val;
	const char *conf_role;
	int conf_index = -1;
	char buf[2000], *pos, *pos2;
	char buf2[200];
	char conf_ssid[100];
	char conf_pass[100];
	char csrattrs[200];
	char pkex_identifier[200];
	struct wpa_ctrl *ctrl;
	int res;
	unsigned int old_timeout;
	int own_pkex_id = -1;
	const char *ifname = get_station_ifname(dut);
	const char *auth_events[] = {
		"DPP-AUTH-SUCCESS",
		"DPP-AUTH-INIT-FAILED",
		"DPP-NOT-COMPATIBLE",
		"DPP-RESPONSE-PENDING",
		"DPP-SCAN-PEER-QR-CODE",
		"DPP-AUTH-DIRECTION",
		NULL
	};
	const char *conf_events[] = {
		"DPP-CONF-RECEIVED",
		"DPP-CONF-SENT",
		"DPP-CONF-FAILED",
		"DPP-MUD-URL",
		NULL
	};
	const char *conn_events[] = {
		"PMKSA-CACHE-ADDED",
		"CTRL-EVENT-CONNECTED",
		NULL
	};
	const char *group_id_str = NULL;
	char group_id[100];
	char conf2[300];
	const char *result;
	int check_mutual = 0;
	int enrollee_ap;
	int enrollee_configurator;
	int force_gas_fragm = 0;
	int not_dpp_akm = 0;
	int akm_use_selector = 0;
	int conn_status;
	int chirp = 0;
	int manual = strcasecmp(type, "ManualDPP") == 0;
	time_t start, now;
	FILE *f;
	char *no_mud_url = "";
	char *mud_url = no_mud_url;

	time(&start);

	if (!wait_conn)
		wait_conn = "no";
	if (!self_conf)
		self_conf = "no";

	if (!prov_role) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing DPPProvisioningRole");
		return STATUS_SENT_ERROR;
	}

	val = get_param(cmd, "DPPConfEnrolleeRole");
	if (val) {
		enrollee_ap = strcasecmp(val, "AP") == 0;
		enrollee_configurator = strcasecmp(val, "Configurator") == 0;
	} else {
		enrollee_ap = sigma_dut_is_ap(dut);
		enrollee_configurator = 0;
	}

	val = get_param(cmd, "DPPNetworkRole");
	if (val) {
		if (strcasecmp(val, "AP") == 0) {
			netrole = "ap";
		} else if (strcasecmp(val, "STA") == 0) {
			netrole = "sta";
		} else if (strcasecmp(val, "Configurator") == 0) {
			netrole = "configurator";
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported DPPNetworkRole value");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "DPPChirp");
	if (val)
		chirp = get_enable_disable(val);

	if ((step || frametype) && (!step || !frametype)) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Invalid DPPStep,DPPFrameType,DPPIEAttribute combination");
		return STATUS_SENT_ERROR;
	}

	val = get_param(cmd, "MUDURL");
	if (val) {
		snprintf(buf, sizeof(buf), "SET dpp_mud_url %s", val);
		if (wpa_command(ifname, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set MUD URL");
			return STATUS_SENT_ERROR;
		}
	}

	if (sigma_dut_is_ap(dut)) {
		if (!dut->hostapd_ifname) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"hostapd ifname not specified (-j)");
			return ERROR_SEND_STATUS;
		}
		ifname = dut->hostapd_ifname;

		if (dpp_hostapd_run(dut) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to start hostapd");
			return STATUS_SENT_ERROR;
		}
	}

	if (strcasecmp(prov_role, "Configurator") == 0 ||
	    strcasecmp(prov_role, "Both") == 0) {
		if (dut->dpp_conf_id < 0) {
			snprintf(buf, sizeof(buf),
				 "DPP_CONFIGURATOR_ADD curve=%s",
				 dpp_get_curve(cmd, "DPPSigningKeyECC"));
			if (wpa_command_resp(ifname, buf,
					     buf, sizeof(buf)) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to set up configurator");
				return STATUS_SENT_ERROR;
			}
			dut->dpp_conf_id = atoi(buf);
		}
		if (strcasecmp(prov_role, "Configurator") == 0)
			role = "configurator";
		else
			role = "either";
	} else if (strcasecmp(prov_role, "Enrollee") == 0) {
		role = "enrollee";
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unknown DPPProvisioningRole");
		return STATUS_SENT_ERROR;
	}

	pkex_identifier[0] = '\0';
	if (strcasecmp(bs, "PKEX") == 0) {
		if (sigma_dut_is_ap(dut) && dut->ap_channel != 6) {
			/* For now, have to make operating channel match DPP
			 * listen channel. This should be removed once hostapd
			 * has support for DPP listen on non-operating channel.
			 */
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Update hostapd operating channel to match listen needs");
			dut->ap_channel = 6;

			if (get_driver_type(dut) == DRIVER_OPENWRT) {
				snprintf(buf, sizeof(buf),
					 "iwconfig %s channel %d",
					 dut->hostapd_ifname, dut->ap_channel);
				run_system(dut, buf);
			}

			if (wpa_command(ifname, "SET channel 6") < 0 ||
			    wpa_command(ifname, "DISABLE") < 0 ||
			    wpa_command(ifname, "ENABLE") < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to update channel");
				return STATUS_SENT_ERROR;
			}
		}

		if (!pkex_code) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Missing DPPPKEXCode");
			return STATUS_SENT_ERROR;
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
			return STATUS_SENT_ERROR;
		}
		own_pkex_id = atoi(buf);
	}

	ctrl = open_wpa_mon(ifname);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return ERROR_SEND_STATUS;
	}

	old_timeout = dut->default_timeout;
	val = get_param(cmd, "DPPTimeout");
	if (val && atoi(val) > 0) {
		dut->default_timeout = atoi(val);
		sigma_dut_print(dut, DUT_MSG_DEBUG, "DPP timeout: %u",
				dut->default_timeout);
	}

	val = get_param(cmd, "DPPStatusQuery");
	conn_status = val && strcasecmp(val, "Yes") == 0;

	conf_ssid[0] = '\0';
	conf_pass[0] = '\0';
	csrattrs[0] = '\0';
	group_id[0] = '\0';
	conf2[0] = '\0';
	if (!enrollee_configurator) {
		val = get_param(cmd, "DPPConfIndex");
		if (val)
			conf_index = atoi(val);
	}
	switch (conf_index) {
	case -1:
		if (enrollee_configurator)
			conf_role = "configurator";
		else
			conf_role = NULL;
		break;
	case 1:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		if (enrollee_ap) {
			conf_role = "ap-dpp";
		} else {
			conf_role = "sta-dpp";
		}
		group_id_str = "DPPGROUP_DPP_INFRA";
		break;
	case 2:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		snprintf(conf_pass, sizeof(conf_pass),
			 "psk=10506e102ad1e7f95112f6b127675bb8344dacacea60403f3fa4055aec85b0fc");
		if (enrollee_ap)
			conf_role = "ap-psk";
		else
			conf_role = "sta-psk";
		break;
	case 3:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		ascii2hexstr("ThisIsDppPassphrase", buf);
		res = snprintf(conf_pass, sizeof(conf_pass), "pass=%s", buf);
		if (res < 0 || res >= sizeof(conf_pass))
			goto err;
		if (enrollee_ap)
			conf_role = "ap-psk";
		else
			conf_role = "sta-psk";
		break;
	case 4:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		if (enrollee_ap) {
			conf_role = "ap-dpp";
		} else {
			conf_role = "sta-dpp";
		}
		group_id_str = "DPPGROUP_DPP_INFRA2";
		break;
	case 5:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		ascii2hexstr("ThisIsDppPassphrase", buf);
		res = snprintf(conf_pass, sizeof(conf_pass), "pass=%s", buf);
		if (res < 0 || res >= sizeof(conf_pass))
			goto err;
		if (enrollee_ap)
			conf_role = "ap-sae";
		else
			conf_role = "sta-sae";
		break;
	case 6:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		ascii2hexstr("ThisIsDppPassphrase", buf);
		res = snprintf(conf_pass, sizeof(conf_pass), "pass=%s", buf);
		if (res < 0 || res >= sizeof(conf_pass))
			goto err;
		if (enrollee_ap)
			conf_role = "ap-psk-sae";
		else
			conf_role = "sta-psk-sae";
		break;
	case 7:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		if (enrollee_ap) {
			conf_role = "ap-dpp";
		} else {
			conf_role = "sta-dpp";
		}
		group_id_str = "DPPGROUP_DPP_INFRA";
		force_gas_fragm = 1;
		break;
	case 8:
	case 9:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		ascii2hexstr("This_is_legacy_password", buf);
		res = snprintf(conf_pass, sizeof(conf_pass), "pass=%s", buf);
		if (res < 0 || res >= sizeof(conf_pass))
			goto err;
		if (enrollee_ap) {
			conf_role = "ap-dpp+psk+sae";
		} else {
			conf_role = "sta-dpp+psk+sae";
		}
		group_id_str = "DPPGROUP_DPP_INFRA1";
		if (conf_index == 9)
			akm_use_selector = 1;
		break;
	case 10:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		if (enrollee_ap)
			conf_role = "ap-dpp";
		else
			conf_role = "sta-dpp";
		group_id_str = "DPPGROUP_DPP_INFRA1";
		ascii2hexstr("DPPNET02", buf);
		ascii2hexstr("This_is_legacy_password", buf2);
		res = snprintf(conf2, sizeof(conf2),
			       " @CONF-OBJ-SEP@ conf=%s-dpp+psk+sae ssid=%s pass=%s group_id=DPPGROUP_DPP_INFRA2",
			       enrollee_ap ? "ap" : "sta", buf, buf2);
		if (res < 0 || res >= sizeof(conf2))
			goto err;
		break;
	case 11:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		if (enrollee_ap) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,dot1x AKM provisioning not supported for AP");
			goto out;
		}
		conf_role = "sta-dot1x";
		snprintf(buf, sizeof(buf), "%s/dpp-ca-csrattrs",
			 sigma_cert_path);
		f = fopen(buf, "r");
		if (f) {
			size_t len;
			int r;

			len = fread(buf, 1, sizeof(buf), f);
			fclose(f);
			if (len >= sizeof(buf)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,No room for csrAttrs");
				goto out;
			}
			buf[len] = '\0';
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Use csrAttrs from file");
			r = snprintf(csrattrs, sizeof(csrattrs),
				     " csrattrs=%s", buf);
			if (r <= 0 || r >= sizeof(csrattrs)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,No room for csrAttrs");
				goto out;
			}
		} else {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Use default csrAttrs");
			snprintf(csrattrs, sizeof(csrattrs), "%s",
				 " csrattrs=MAsGCSqGSIb3DQEJBw==");
		}
		break;
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported DPPConfIndex");
		goto out;
	}

	if (group_id_str)
		snprintf(group_id, sizeof(group_id), " group_id=%s",
			 group_id_str);

	if (force_gas_fragm) {
		char spaces[1500];

		memset(spaces, ' ', sizeof(spaces));
		spaces[sizeof(spaces) - 1] = '\0';

		snprintf(buf, sizeof(buf),
			 "SET dpp_discovery_override {\"ssid\":\"DPPNET01\"}%s",
			 spaces);
		if (wpa_command(ifname, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set discovery override");
			goto out;
		}
	}

	if (step) {
		int test;

		test = dpp_get_test(step, frametype, attr);
		if (test <= 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported DPPStep/DPPFrameType/DPPIEAttribute");
			goto out;
		}

		snprintf(buf, sizeof(buf), "SET dpp_test %d", test);
		if (wpa_command(ifname, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set dpp_test");
			goto out;
		}
	} else {
		wpa_command(ifname, "SET dpp_test 0");
	}

	if (strcasecmp(self_conf, "Yes") == 0) {
		if (strcasecmp(prov_role, "Configurator") != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid DPPSelfConfigure use - only allowed for Configurator role");
			goto out;
		}
		if (!conf_role) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Missing DPPConfIndex");
			goto out;
		}

		snprintf(buf, sizeof(buf),
			 "DPP_CONFIGURATOR_SIGN  conf=%s %s %s configurator=%d",
			 conf_role, conf_ssid, conf_pass, dut->dpp_conf_id);
		if (wpa_command(ifname, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to initiate DPP self-configuration");
			goto out;
		}
		if (sigma_dut_is_ap(dut))
			goto update_ap;
		goto wait_connect;
	} else if (manual && strcasecmp(bs, "NFC") == 0) {
		const char *val = get_param(cmd, "DPPNFCInit");
		int init = !val || atoi(val) > 0;
		pid_t pid;
		int pid_status;
		int enrollee = 0;
		int tag_read = 0;
		int tag_write_uri = 0;
		int tag_write_hs = 0;
		const char *tx_rx_events[] = { "DPP-TX", "DPP-RX", NULL };
		const char *chan_list, *alt_chan_list;
		char chan_list2[200], alt_chan_list2[200];

		if (strcasecmp(prov_role, "Configurator") == 0 ||
		    strcasecmp(prov_role, "Both") == 0) {
			if (!conf_role) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Missing DPPConfIndex");
				goto out;
			}
			snprintf(buf, sizeof(buf),
				 "SET dpp_configurator_params  conf=%s %s %s configurator=%d%s%s%s%s%s",
				 conf_role, conf_ssid, conf_pass,
				 dut->dpp_conf_id, group_id,
				 akm_use_selector ? " akm_use_selector=1" : "",
				 conn_status ? " conn_status=1" : "",
				 csrattrs, conf2);
			if (wpa_command(ifname, buf) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to set configurator parameters");
				goto out;
			}
			snprintf(buf, sizeof(buf),
				 "conf=%s %s %s configurator=%d%s%s%s%s%s",
				 conf_role, conf_ssid, conf_pass,
				 dut->dpp_conf_id, group_id,
				 akm_use_selector ? " akm_use_selector=1" : "",
				 conn_status ? " conn_status=1" : "", csrattrs,
				 conf2);
		} else {
			buf[0] = '\0';
			enrollee = 1;
		}

		val = get_param(cmd, "DPPNFCTag");
		if (val) {
			if (strcasecmp(val, "Read") == 0) {
				tag_read = 1;
			} else if (strcasecmp(val, "Write-HS") == 0) {
				tag_write_hs = 1;
			} else if (strcasecmp(val, "Write-URI") == 0) {
				tag_write_uri = 1;
			} else {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Unsupported DPPNFCTag value");
				goto out;
			}
		}

		chan_list = get_param(cmd, "DPPChannelList");
		if (chan_list) {
			strlcpy(chan_list2, chan_list, sizeof(chan_list2));
			for (pos = chan_list2; *pos; pos++) {
				if (*pos == ' ')
					*pos = ',';
			}
		}
		alt_chan_list = get_param(cmd, "DPPNFCAltChannelList");
		if (alt_chan_list) {
			strlcpy(alt_chan_list2, alt_chan_list,
				sizeof(alt_chan_list2));
			for (pos = alt_chan_list2; *pos; pos++) {
				if (*pos == ' ')
					*pos = ',';
			}
		}

		run_system(dut, "killall dpp-nfc.py");
		sigma_dut_print(dut, DUT_MSG_INFO, "Manual NFC operation");
		if (!file_exists("dpp-nfc.py")) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,dpp-nfc.py not found");
			goto out;
		}

		pid = fork();
		if (pid < 0) {
			perror("fork");
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,fork() failed");
			goto out;
		}

		if (pid == 0) {
			char *argv[100];
			int pos = 0;

			argv[pos++] = "dpp-nfc.py";
			argv[pos++] = "--only-one";
			argv[pos++] = "--no-input";
			argv[pos++] = "-i";
			argv[pos++] = (char *) ifname;
			argv[pos++] = "--ctrl";
			argv[pos++] = sigma_wpas_ctrl;
			argv[pos++] = enrollee ? "--enrollee" :
				"--configurator";
			argv[pos++] = "--config-params";
			argv[pos++] = buf;
			if (chan_list && strcmp(chan_list, "0/0") != 0) {
				argv[pos++] = "--chan";
				argv[pos++] = chan_list2;
			}
			if (alt_chan_list &&
			    strcmp(alt_chan_list, "0/0") != 0) {
				argv[pos++] = "--altchan";
				argv[pos++] = alt_chan_list2;
			}
			if (init)
				argv[pos++] = "-I";
			if (netrole) {
				argv[pos++] = "--netrole";
				argv[pos++] = (char *) netrole;
			}
			if (tag_read || tag_write_hs || tag_write_uri)
				argv[pos++] = "--no-wait";
			if (!tag_read && !tag_write_hs && !tag_write_uri)
				argv[pos++] = "--handover-only";
			if (tag_read)
				argv[pos++] = "--tag-read-only";
			else if (tag_write_hs)
				argv[pos++] = "write-nfc-hs";
			else if (tag_write_uri)
				argv[pos++] = "write-nfc-uri";
			argv[pos] = NULL;

			execv("./dpp-nfc.py", argv);
			perror("execv");
			exit(0);
			return -1;
		}

		usleep(300000);
		for (;;) {
			if (waitpid(pid, &pid_status, WNOHANG) > 0) {
				int status = WEXITSTATUS(pid_status);

				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"dpp-nfc.py exited (status %d)",
						status);
				if (status == 1) {
					send_resp(dut, conn, SIGMA_ERROR,
						  "errorCode,dpp-nfc.py operation failed");
					goto out;
				}
				break;
			}

			time(&now);
			if ((unsigned int) (now - start) >=
			    dut->default_timeout) {
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"dpp-nfc.py did not exit within timeout - stop it");
				kill(pid, SIGTERM);
				waitpid(pid, &pid_status, 0);
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,dpp-nfc.py did not complete within timeout");
				goto out;
			}

			old_timeout = dut->default_timeout;
			dut->default_timeout = 2;

			res = get_wpa_cli_events(dut, ctrl, tx_rx_events,
						buf, sizeof(buf));
			dut->default_timeout = old_timeout;
			if (res >= 0) {
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"DPP exchange started");
				usleep(500000);
				kill(pid, SIGTERM);
				waitpid(pid, &pid_status, 0);
				break;
			}
		}
	} else if ((nfc_handover &&
		    strcasecmp(nfc_handover, "Negotiated_Requestor") == 0) ||
		   ((!nfc_handover ||
		     strcasecmp(nfc_handover, "Static") == 0) &&
		    auth_role && strcasecmp(auth_role, "Initiator") == 0)) {
		char own_txt[20];
		int dpp_peer_bootstrap = -1;
		char neg_freq[30];

		val = get_param(cmd, "DPPAuthDirection");
		check_mutual = val && strcasecmp(val, "Mutual") == 0;

		neg_freq[0] = '\0';
		val = get_param(cmd, "DPPSubsequentChannel");
		if (val) {
			int opclass, channel, freq;

			opclass = atoi(val);
			val = strchr(val, '/');
			if (opclass == 0 || !val) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Invalid DPPSubsequentChannel");
				goto out;
			}
			val++;
			channel = atoi(val);

			/* Ignoring opclass for now; could use it here for more
			 * robust frequency determination. */
			freq = channel_to_freq(dut, channel);
			if (!freq) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Unsupported DPPSubsequentChannel channel");
				goto out;
			}
			snprintf(neg_freq, sizeof(neg_freq), " neg_freq=%d",
				 freq);
		}

		if (strcasecmp(bs, "QR") == 0) {
			if (!dut->dpp_peer_uri) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Missing peer bootstrapping info");
				goto out;
			}

			snprintf(buf, sizeof(buf), "DPP_QR_CODE %s",
				 dut->dpp_peer_uri);
			if (wpa_command_resp(ifname, buf, buf,
					     sizeof(buf)) < 0 ||
			    strncmp(buf, "FAIL", 4) == 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to parse URI");
				goto out;
			}
			dpp_peer_bootstrap = atoi(buf);
		} else if (strcasecmp(bs, "NFC") == 0 && nfc_handover &&
			   strcasecmp(nfc_handover, "Static") == 0) {
			if (!dut->dpp_peer_uri) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Missing peer bootstrapping info");
				goto out;
			}

			snprintf(buf, sizeof(buf), "DPP_NFC_URI %s",
				 dut->dpp_peer_uri);
			if (wpa_command_resp(ifname, buf,
					     buf, sizeof(buf)) < 0 ||
			    strncmp(buf, "FAIL", 4) == 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to process URI from NFC Tag");
				goto out;
			}
			dpp_peer_bootstrap = atoi(buf);
		} else if (strcasecmp(bs, "NFC") == 0) {
			if (!dut->dpp_peer_uri) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Missing peer bootstrapping info");
				goto out;
			}
			if (dut->dpp_local_bootstrap < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Missing own bootstrapping info");
				goto out;
			}

			snprintf(buf, sizeof(buf),
				 "DPP_NFC_HANDOVER_SEL own=%d uri=%s",
				 dut->dpp_local_bootstrap, dut->dpp_peer_uri);
			if (wpa_command_resp(ifname, buf,
					     buf, sizeof(buf)) < 0 ||
			    strncmp(buf, "FAIL", 4) == 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to process NFC Handover Select");
				goto out;
			}
			dpp_peer_bootstrap = atoi(buf);
		}

		if (dut->dpp_local_bootstrap >= 0)
			snprintf(own_txt, sizeof(own_txt), " own=%d",
				 dut->dpp_local_bootstrap);
		else
			own_txt[0] = '\0';
		if (chirp) {
			int freq = 2437; /* default: channel 6 */

			val = get_param(cmd, "DPPChirpChannel");
			if (val) {
				freq = channel_to_freq(dut, atoi(val));
				if (!freq) {
					send_resp(dut, conn, SIGMA_ERROR,
						  "errorCode,Unsupported DPPChirpChannel channel");
					goto out;
				}
			}

			if (strcasecmp(prov_role, "Configurator") == 0 ||
			    strcasecmp(prov_role, "Both") == 0) {
				if (!conf_role) {
					send_resp(dut, conn, SIGMA_ERROR,
						  "errorCode,Missing DPPConfIndex");
					goto out;
				}
				snprintf(buf, sizeof(buf),
					 "SET dpp_configurator_params  conf=%s %s %s configurator=%d%s%s%s%s%s",
					 conf_role, conf_ssid, conf_pass,
					 dut->dpp_conf_id, group_id,
					 akm_use_selector ?
					 " akm_use_selector=1" : "",
					 conn_status ? " conn_status=1" : "",
					 csrattrs, conf2);
				if (wpa_command(ifname, buf) < 0) {
					send_resp(dut, conn, SIGMA_ERROR,
						  "errorCode,Failed to set configurator parameters");
					goto out;
				}
			}

			if (tcp && strcasecmp(tcp, "yes") == 0) {
				wpa_command(ifname, "DPP_STOP_LISTEN");
				snprintf(buf, sizeof(buf),
					 "DPP_CONTROLLER_START");
			} else {
				snprintf(buf, sizeof(buf),
					 "DPP_LISTEN %d role=%s%s%s",
					 freq, role,
					 netrole ? " netrole=" : "",
					 netrole ? netrole : "");
			}
		} else if ((strcasecmp(bs, "QR") == 0 ||
			    strcasecmp(bs, "NFC") == 0) &&
			   (strcasecmp(prov_role, "Configurator") == 0 ||
			    strcasecmp(prov_role, "Both") == 0)) {
			if (!conf_role) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Missing DPPConfIndex");
				goto out;
			}
			snprintf(buf, sizeof(buf),
				 "DPP_AUTH_INIT peer=%d%s role=%s%s%s conf=%s %s %s configurator=%d%s%s%s%s%s%s%s%s",
				 dpp_peer_bootstrap, own_txt, role,
				 netrole ? " netrole=" : "",
				 netrole ? netrole : "",
				 conf_role, conf_ssid, conf_pass,
				 dut->dpp_conf_id, neg_freq, group_id,
				 akm_use_selector ? " akm_use_selector=1" : "",
				 conn_status ? " conn_status=1" : "",
				 tcp ? " tcp_addr=" : "",
				 tcp ? tcp : "",
				 csrattrs, conf2);
		} else if (tcp && (strcasecmp(bs, "QR") == 0 ||
				   strcasecmp(bs, "NFC") == 0)) {
			snprintf(buf, sizeof(buf),
				 "DPP_AUTH_INIT peer=%d%s role=%s%s%s tcp_addr=%s%s%s",
				 dpp_peer_bootstrap, own_txt, role,
				 netrole ? " netrole=" : "",
				 netrole ? netrole : "",
				 tcp, neg_freq, group_id);
		} else if (strcasecmp(bs, "QR") == 0 ||
			   strcasecmp(bs, "NFC") == 0) {
			snprintf(buf, sizeof(buf),
				 "DPP_AUTH_INIT peer=%d%s role=%s%s%s%s%s",
				 dpp_peer_bootstrap, own_txt, role,
				 netrole ? " netrole=" : "",
				 netrole ? netrole : "",
				 neg_freq, group_id);
		} else if (strcasecmp(bs, "PKEX") == 0 &&
			   (strcasecmp(prov_role, "Configurator") == 0 ||
			    strcasecmp(prov_role, "Both") == 0)) {
			if (!conf_role) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Missing DPPConfIndex");
				goto out;
			}
			snprintf(buf, sizeof(buf),
				 "DPP_PKEX_ADD own=%d init=1 role=%s conf=%s %s %s configurator=%d%s %scode=%s",
				 own_pkex_id, role, conf_role,
				 conf_ssid, conf_pass, dut->dpp_conf_id,
				 csrattrs, pkex_identifier, pkex_code);
		} else if (strcasecmp(bs, "PKEX") == 0) {
			snprintf(buf, sizeof(buf),
				 "DPP_PKEX_ADD own=%d init=1 role=%s %scode=%s",
				 own_pkex_id, role, pkex_identifier, pkex_code);
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported DPPBS");
			goto out;
		}
		if (wpa_command(ifname, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to initiate DPP authentication");
			goto out;
		}
	} else if ((nfc_handover &&
		    strcasecmp(nfc_handover, "Negotiated_Selector") == 0) ||
		   ((!nfc_handover ||
		     strcasecmp(nfc_handover, "Static") == 0) &&
		    auth_role && strcasecmp(auth_role, "Responder") == 0)) {
		const char *delay_qr_resp;
		int mutual;
		int freq = 2462; /* default: channel 11 */

		if (sigma_dut_is_ap(dut) && dut->hostapd_running &&
		    dut->ap_oper_chn)
			freq = channel_to_freq(dut, dut->ap_channel);

		if (strcasecmp(bs, "PKEX") == 0) {
			/* default: channel 6 for PKEX */
			freq = 2437;
		}

		delay_qr_resp = get_param(cmd, "DPPDelayQRResponse");

		val = get_param(cmd, "DPPAuthDirection");
		mutual = val && strcasecmp(val, "Mutual") == 0;

		val = get_param(cmd, "DPPListenChannel");
		if (val) {
			freq = channel_to_freq(dut, atoi(val));
			if (freq == 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Unsupported DPPListenChannel value");
				goto out;
			}

			if (sigma_dut_is_ap(dut) && !chirp &&
			    dut->ap_start_disabled &&
			    atoi(val) != dut->ap_channel) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"Use requested listen channel as the initial operating channel");
				snprintf(buf, sizeof(buf), "SET channel %d",
					 atoi(val));
				wpa_command(ifname, buf);
			}
		}

		if (sigma_dut_is_ap(dut) && dpp_hostapd_beacon(dut) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to start AP mode listen");
			goto out;
		}

		if (strcasecmp(bs, "NFC") == 0 && nfc_handover &&
		    strcasecmp(nfc_handover, "Static") == 0) {
			/* No steps needed here - waiting for peer to initiate
			 * once it reads the URI from the NFC Tag */
		} else if (strcasecmp(bs, "NFC") == 0) {
			if (!dut->dpp_peer_uri) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Missing peer bootstrapping info");
				goto out;
			}
			if (dut->dpp_local_bootstrap < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Missing own bootstrapping info");
				goto out;
			}

			snprintf(buf, sizeof(buf),
				 "DPP_NFC_HANDOVER_REQ own=%d uri=%s",
				 dut->dpp_local_bootstrap, dut->dpp_peer_uri);
			if (wpa_command(ifname, buf) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to process NFC Handover Request");
				goto out;
			}

			snprintf(buf, sizeof(buf), "DPP_BOOTSTRAP_INFO %d",
				 dut->dpp_local_bootstrap);
			if (wpa_command_resp(ifname, buf,
					     buf, sizeof(buf)) < 0 ||
			    strncmp(buf, "FAIL", 4) == 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to get bootstrap information");
				goto out;
			}
			pos = buf;
			while (pos) {
				pos2 = strchr(pos, '\n');
				if (pos2)
					*pos2 = '\0';
				if (strncmp(pos, "use_freq=", 9) == 0) {
					freq = atoi(pos + 9);
					sigma_dut_print(dut, DUT_MSG_DEBUG,
							"DPP negotiation frequency from NFC handover: %d MHz",
							freq);
					break;
				}

				if (!pos2)
					break;
				pos = pos2 + 1;
			}
		} else if (!delay_qr_resp && dut->dpp_peer_uri) {
			snprintf(buf, sizeof(buf), "DPP_QR_CODE %s",
				 dut->dpp_peer_uri);
			if (wpa_command_resp(ifname, buf, buf,
					     sizeof(buf)) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to parse URI");
				goto out;
			}
		}

		if (strcasecmp(prov_role, "Configurator") == 0) {
			if (!conf_role) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Missing DPPConfIndex");
				goto out;
			}
			snprintf(buf, sizeof(buf),
				 "SET dpp_configurator_params  conf=%s %s %s configurator=%d%s%s%s%s%s",
				 conf_role, conf_ssid, conf_pass,
				 dut->dpp_conf_id, group_id,
				 akm_use_selector ? " akm_use_selector=1" : "",
				 conn_status ? " conn_status=1" : "", csrattrs,
				 conf2);
			if (wpa_command(ifname, buf) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to set configurator parameters");
				goto out;
			}
		}
		if (strcasecmp(bs, "PKEX") == 0) {
			snprintf(buf, sizeof(buf),
				 "DPP_PKEX_ADD own=%d role=%s %scode=%s",
				 own_pkex_id, role, pkex_identifier, pkex_code);
			if (wpa_command(ifname, buf) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to configure DPP PKEX");
				goto out;
			}
		}

		if (chirp) {
			snprintf(buf, sizeof(buf),
				 "DPP_CHIRP own=%d iter=10 listen=%d",
				 dut->dpp_local_bootstrap, freq);
		} else if (tcp && strcasecmp(tcp, "yes") == 0) {
			snprintf(buf, sizeof(buf), "DPP_CONTROLLER_START%s",
				 (strcasecmp(bs, "QR") == 0 && mutual) ?
				 " qr=mutual" : "");
		} else {
			snprintf(buf, sizeof(buf),
				 "DPP_LISTEN %d role=%s%s%s%s",
				 freq, role,
				 (strcasecmp(bs, "QR") == 0 && mutual) ?
				 " qr=mutual" : "",
				 netrole ? " netrole=" : "",
				 netrole ? netrole : "");
		}
		if (wpa_command(ifname, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to start DPP listen/chirp");
			goto out;
		}

		if (!(tcp && strcasecmp(tcp, "yes") == 0) &&
		    get_driver_type(dut) == DRIVER_OPENWRT) {
			snprintf(buf, sizeof(buf), "iwconfig %s channel %d",
				 dut->hostapd_ifname, freq_to_channel(freq));
			run_system(dut, buf);
		}

		if (delay_qr_resp && mutual && dut->dpp_peer_uri) {
			int wait_time = atoi(delay_qr_resp);

			res = get_wpa_cli_events(dut, ctrl, auth_events,
						 buf, sizeof(buf));
			if (res < 0) {
				send_resp(dut, conn, SIGMA_COMPLETE,
					  "BootstrapResult,OK,AuthResult,Timeout");
				goto out;
			}
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"DPP auth result: %s", buf);
			if (strstr(buf, "DPP-SCAN-PEER-QR-CODE") == NULL) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,No scan request for peer QR Code seen");
				goto out;
			}
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Waiting %d second(s) before processing peer URI",
					wait_time);
			sleep(wait_time);

			snprintf(buf, sizeof(buf), "DPP_QR_CODE %s",
				 dut->dpp_peer_uri);
			if (wpa_command_resp(ifname, buf, buf,
					     sizeof(buf)) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to parse URI");
				goto out;
			}
		} else if (mutual && action_type &&
			   strcasecmp(action_type, "ManualDPP") == 0) {
			res = get_wpa_cli_events(dut, ctrl, auth_events,
						 buf, sizeof(buf));
			if (res < 0) {
				send_resp(dut, conn, SIGMA_COMPLETE,
					  "BootstrapResult,OK,AuthResult,Timeout");
				goto out;
			}
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"DPP auth result: %s", buf);
			if (strstr(buf, "DPP-NOT-COMPATIBLE")) {
			    send_resp(dut, conn, SIGMA_COMPLETE,
				      "BootstrapResult,OK,AuthResult,ROLES_NOT_COMPATIBLE");
			    goto out;
			}

			if (strstr(buf, "DPP-SCAN-PEER-QR-CODE") == NULL) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,No scan request for peer QR Code seen");
				goto out;
			}

			if (dpp_scan_peer_qrcode(dut) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to scan peer QR Code");
				goto out;
			}

			snprintf(buf, sizeof(buf), "DPP_QR_CODE %s",
				 dut->dpp_peer_uri);
			if (wpa_command_resp(ifname, buf, buf,
					     sizeof(buf)) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to parse URI");
				goto out;
			}
		}
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unknown DPPAuthRole");
		goto out;
	}

	if (step && strcasecmp(step, "Timeout") == 0) {
		result = "errorCode,Unexpected state";

		if (strcasecmp(frametype, "PKEXExchangeResponse") == 0) {
			if (dpp_wait_rx(dut, ctrl, 8, -1) < 0)
				result = "BootstrapResult,Timeout";
			else
				result = "BootstrapResult,Errorsent";
		}

		if (strcasecmp(frametype, "PKEXCRRequest") == 0) {
			if (dpp_wait_rx(dut, ctrl, 9, -1) < 0)
				result = "BootstrapResult,Timeout";
			else
				result = "BootstrapResult,Errorsent";
		}

		if (strcasecmp(frametype, "PKEXCRResponse") == 0) {
			if (dpp_wait_rx(dut, ctrl, 10, -1) < 0)
				result = "BootstrapResult,Timeout";
			else
				result = "BootstrapResult,Errorsent";
		}

		if (strcasecmp(frametype, "AuthenticationRequest") == 0) {
			if (dpp_wait_rx(dut, ctrl, 0, -1) < 0)
				result = "BootstrapResult,OK,AuthResult,Timeout";
			else
				result = "BootstrapResult,OK,AuthResult,Errorsent";
		}

		if (strcasecmp(frametype, "AuthenticationResponse") == 0) {
			if (dpp_wait_rx(dut, ctrl, 1, -1) < 0)
				result = "BootstrapResult,OK,AuthResult,Timeout";
			else
				result = "BootstrapResult,OK,AuthResult,Errorsent";
		}

		if (strcasecmp(frametype, "AuthenticationConfirm") == 0) {
			if (auth_role &&
			    strcasecmp(auth_role, "Initiator") == 0) {
				/* This special case of DPPStep,Timeout with
				 * DPPFrameType,AuthenticationConfirm on an
				 * Initiator is used to cover need for stopping
				 * the Initiator/Enrollee from sending out
				 * Configuration Request message. */
				if (strcasecmp(prov_role, "Enrollee") != 0) {
					send_resp(dut, conn, SIGMA_ERROR,
						  "errorCode,Unexpected use of timeout after AuthenticationConfirm TX in Configurator role");
					goto out;
				}
				if (check_mutual &&
				    dpp_process_auth_response(
					    dut, conn, ctrl, auth_events,
					    action_type, check_mutual,
					    buf, sizeof(buf)) < 0)
					goto out;
				if (dpp_wait_tx_status(dut, ctrl, 2) < 0)
					result = "BootstrapResult,OK,AuthResult,Timeout";
				else
					result = "BootstrapResult,OK,AuthResult,Errorsent,LastFrameReceived,AuthenticationResponse";
			} else {
				if (dpp_wait_rx(dut, ctrl, 2, -1) < 0)
					result = "BootstrapResult,OK,AuthResult,Timeout";
				else
					result = "BootstrapResult,OK,AuthResult,Errorsent,LastFrameReceived,AuthenticationConfirm";
			}
		}

		if (strcasecmp(frametype, "ConfigurationRequest") == 0) {
			if (get_wpa_cli_event(dut, ctrl, "DPP-CONF-FAILED",
					      buf, sizeof(buf)) < 0)
				result = "BootstrapResult,OK,AuthResult,OK,ConfResult,Timeout";
			else
				result = "BootstrapResult,OK,AuthResult,OK,ConfResult,Errorsent";
		}

		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	if (frametype && strcasecmp(frametype, "PKEXExchangeRequest") == 0) {
		if (dpp_wait_tx_status(dut, ctrl, 7) < 0)
			result = "BootstrapResult,Timeout";
		else
			result = "BootstrapResult,Errorsent";
		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	if (frametype && strcasecmp(frametype, "PKEXExchangeResponse") == 0) {
		if (dpp_wait_tx_status(dut, ctrl, 8) < 0)
			result = "BootstrapResult,Timeout";
		else
			result = "BootstrapResult,Errorsent";
		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	if (frametype && strcasecmp(frametype, "PKEXCRRequest") == 0) {
		if (dpp_wait_tx_status(dut, ctrl, 9) < 0)
			result = "BootstrapResult,Timeout";
		else
			result = "BootstrapResult,Errorsent";
		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	if (frametype && strcasecmp(frametype, "PKEXCRResponse") == 0) {
		if (dpp_wait_tx_status(dut, ctrl, 10) < 0)
			result = "BootstrapResult,Timeout";
		else
			result = "BootstrapResult,Errorsent";
		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	if (!frametype && strcasecmp(bs, "PKEX") == 0 &&
	    auth_role && strcasecmp(auth_role, "Responder") == 0) {
		if (dpp_wait_tx_status(dut, ctrl, 10) < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "BootstrapResult,Timeout");
			goto out;
		}
	}

	if (!frametype && strcasecmp(bs, "PKEX") == 0 &&
	    auth_role && strcasecmp(auth_role, "Initiator") == 0) {
		if (dpp_wait_tx(dut, ctrl, 0) < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "BootstrapResult,Timeout");
			goto out;
		}
	}

	if (frametype && strcasecmp(frametype, "AuthenticationRequest") == 0) {
		if (dpp_wait_tx_status(dut, ctrl, 0) < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "BootstrapResult,OK,AuthResult,Timeout");
			goto out;
		}

		if (dpp_wait_rx(dut, ctrl, 1, 5) < 0)
			result = "BootstrapResult,OK,AuthResult,Errorsent,LastFrameReceived,None";
		else if	(get_wpa_cli_events(dut, ctrl, auth_events,
					    buf, sizeof(buf)) >= 0 &&
			 strstr(buf, "DPP-RESPONSE-PENDING") != NULL)
			result = "BootstrapResult,OK,AuthResult,Errorsent,LastFrameReceived,AuthenticationResponseWithStatusPending";
		else
			result = "BootstrapResult,OK,AuthResult,Errorsent,LastFrameReceived,AuthenticationResponse";
		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	if (frametype && strcasecmp(frametype, "AuthenticationResponse") == 0) {
		if (dpp_wait_tx_status(dut, ctrl, 1) < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "BootstrapResult,OK,AuthResult,Timeout");
			goto out;
		}

		if (dpp_wait_rx(dut, ctrl, 2, 5) < 0)
			result = "BootstrapResult,OK,AuthResult,Errorsent,LastFrameReceived,AuthenticationRequest";
		else
			result = "BootstrapResult,OK,AuthResult,Errorsent,LastFrameReceived,AuthenticationConfirm";
		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	if (dpp_process_auth_response(dut, conn, ctrl, auth_events, action_type,
				      check_mutual, buf, sizeof(buf)) < 0)
		goto out;

	if (frametype && strcasecmp(frametype, "AuthenticationConfirm") == 0) {
		if (dpp_wait_tx_status(dut, ctrl, 2) < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "BootstrapResult,OK,AuthResult,Timeout");
			goto out;
		}

		if (dpp_wait_rx_conf_req(dut, ctrl, 5) < 0)
			result = "BootstrapResult,OK,AuthResult,Errorsent,LastFrameReceived,AuthenticationResponse";
		else
			result = "BootstrapResult,OK,AuthResult,Errorsent,LastFrameReceived,ConfigurationRequest";
		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	if (strstr(buf, "DPP-AUTH-DIRECTION")) {
		res = get_wpa_cli_events(dut, ctrl, auth_events,
					 buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "BootstrapResult,OK,AuthResult,Timeout");
			goto out;
		}

		sigma_dut_print(dut, DUT_MSG_DEBUG, "DPP auth result: %s", buf);
	}

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

	if (frametype && strcasecmp(frametype, "ConfigurationRequest") == 0) {
		res = get_wpa_cli_event(dut, ctrl, "GAS-QUERY-DONE",
					buf, sizeof(buf));
		if (res < 0)
			result = "BootstrapResult,OK,AuthResult,OK,ConfResult,Timeout";
		else
			result = "BootstrapResult,OK,AuthResult,OK,ConfResult,Errorsent";
		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	if (frametype && strcasecmp(frametype, "ConfigurationResponse") == 0) {
		res = get_wpa_cli_events(dut, ctrl, conf_events,
					 buf, sizeof(buf));
		if (res >= 0 && strstr(buf, "DPP-MUD-URL "))
			res = get_wpa_cli_events(dut, ctrl, conf_events,
						 buf, sizeof(buf));
		if (res < 0)
			result = "BootstrapResult,OK,AuthResult,OK,ConfResult,Timeout";
		else
			result = "BootstrapResult,OK,AuthResult,OK,ConfResult,Errorsent,LastFrameReceived,ConfigurationRequest";
		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	if (strcasecmp(prov_role, "Configurator") == 0 && csrattrs[0]) {
		res = get_wpa_cli_event(dut, ctrl, "DPP-CSR", buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,No CSR received from Enrollee");
			res = STATUS_SENT_ERROR;
			goto out;
		}

		if (dpp_process_csr(dut, ifname, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to process CSR");
			res = STATUS_SENT_ERROR;
			goto out;
		}
	}

	res = get_wpa_cli_events(dut, ctrl, conf_events, buf, sizeof(buf));
	if (res >= 0 && strstr(buf, "DPP-MUD-URL ")) {
		size_t url_len;

		pos = strchr(buf, ' ');
		if (!pos)
			goto err;
		pos++;
		url_len = strlen(buf);
		mud_url = malloc(9 + url_len);
		if (!mud_url)
			goto err;
		memcpy(mud_url, ",MUDURL,", 8);
		memcpy(mud_url + 8, pos, url_len + 1);

		res = get_wpa_cli_events(dut, ctrl, conf_events,
					 buf, sizeof(buf));
	}
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

	if (conn_status && strstr(buf, "DPP-CONF-SENT") &&
	    strstr(buf, "wait_conn_status=1")) {
		res = get_wpa_cli_event(dut, ctrl, "DPP-CONN-STATUS-RESULT",
					buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,StatusResult,Timeout");
		} else {
			pos = strstr(buf, "result=");
			if (!pos) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Status result value not reported");
			} else {
				pos += 7;
				snprintf(buf, sizeof(buf),
					 "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,StatusResult,%d%s",
					 atoi(pos), mud_url);
				send_resp(dut, conn, SIGMA_COMPLETE, buf);
			}
		}
		goto out;
	}

	if (strcasecmp(prov_role, "Enrollee") == 0 && netrole &&
	    strcmp(netrole, "configurator") == 0) {
		res = get_wpa_cli_event(dut, ctrl, "DPP-CONFIGURATOR-ID",
					buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,No DPP-CONFIGURATOR-ID");
			goto out;
		}
		pos = strchr(buf, ' ');
		if (!pos) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid DPP-CONFIGURATOR-ID");
			goto out;
		}
		pos++;
		dut->dpp_conf_id = atoi(pos);
	} else if (sigma_dut_is_ap(dut) &&
		   strcasecmp(prov_role, "Enrollee") == 0) {
	update_ap:
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
		int netw_id;
		char *pos;

		res = get_wpa_cli_event(dut, ctrl, "DPP-NETWORK-ID",
					buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,No DPP-NETWORK-ID");
			goto out;
		}
		pos = strchr(buf, ' ');
		if (!pos) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid DPP-NETWORK-ID");
			goto out;
		}
		pos++;
		netw_id = atoi(pos);
		snprintf(buf, sizeof(buf), "GET_NETWORK %d key_mgmt", netw_id);
		if (wpa_command_resp(ifname, buf, buf, sizeof(buf)) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Could not fetch provisioned key_mgmt");
			goto out;
		}
		if (strncmp(buf, "SAE", 3) == 0) {
			/* SAE generates PMKSA-CACHE-ADDED event */
			not_dpp_akm = 1;
		}
		dut->dpp_network_id = netw_id;
	wait_connect:
		if (frametype && strcasecmp(frametype,
					    "PeerDiscoveryRequest") == 0) {
			if (dpp_wait_tx_status(dut, ctrl, 5) < 0)
				result = "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,Timeout";
			else
				result = "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,Errorsent";
			send_resp(dut, conn, SIGMA_COMPLETE, result);
			goto out;
		}

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
					  not_dpp_akm ?
					  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkConnectResult,Timeout" :
					  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,OK,NetworkConnectResult,Timeout");
				goto out;
			}
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"DPP connect result: %s", buf);
			if (strstr(buf, "CTRL-EVENT-CONNECTED"))
				send_resp(dut, conn, SIGMA_COMPLETE,
					  not_dpp_akm ?
					  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkConnectResult,OK" :
					  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,OK,NetworkConnectResult,OK");
			else
				send_resp(dut, conn, SIGMA_COMPLETE,
					  not_dpp_akm ?
					  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkConnectResult,Timeout" :
					  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,OK,NetworkConnectResult,Timeout");
			goto out;
		}

		send_resp(dut, conn, SIGMA_COMPLETE,
			  "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkConnectResult,OK");
		goto out;
	} else if (!sigma_dut_is_ap(dut) &&
		   strcasecmp(prov_role, "Enrollee") == 0) {
		/* Store DPP network id for reconfiguration */
		char *pos;
		unsigned int old_timeout;

		old_timeout = dut->default_timeout;
		dut->default_timeout = 3;
		res = get_wpa_cli_event(dut, ctrl, "DPP-NETWORK-ID",
					buf, sizeof(buf));
		dut->default_timeout = old_timeout;

		if (res < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "No DPP-NETWORK-ID");
		} else {
			pos = strchr(buf, ' ');
			if (!pos) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"Invalid DPP-NETWORK-ID");
			} else {
				pos++;
				dut->dpp_network_id = atoi(pos);
			}
		}
	}

	if (strcasecmp(wait_conn, "Yes") == 0 &&
	    frametype && strcasecmp(frametype, "PeerDiscoveryResponse") == 0) {
		if (dpp_wait_tx_status(dut, ctrl, 6) < 0)
			result = "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,Timeout";
		else
			result = "BootstrapResult,OK,AuthResult,OK,ConfResult,OK,NetworkIntroResult,Errorsent";
		send_resp(dut, conn, SIGMA_COMPLETE, result);
		goto out;
	}

	snprintf(buf, sizeof(buf),
		 "BootstrapResult,OK,AuthResult,OK,ConfResult,OK%s", mud_url);
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
out:
	if (mud_url != no_mud_url)
		free(mud_url);
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);
	if (tcp && strcasecmp(tcp, "yes") == 0 &&
	    auth_role && strcasecmp(auth_role, "Responder") == 0)
		wpa_command(ifname, "DPP_CONTROLLER_STOP");
	dut->default_timeout = old_timeout;
	return STATUS_SENT;
err:
	send_resp(dut, conn, SIGMA_ERROR, NULL);
	goto out;
}


static enum sigma_cmd_result dpp_manual_dpp(struct sigma_dut *dut,
					    struct sigma_conn *conn,
					    struct sigma_cmd *cmd)
{
	const char *auth_role = get_param(cmd, "DPPAuthRole");
	const char *self_conf = get_param(cmd, "DPPSelfConfigure");
	enum sigma_cmd_result res = INVALID_SEND_STATUS;
	int success;
	const char *val;
	unsigned int old_timeout;
	const char *bs = get_param(cmd, "DPPBS");

	if (!self_conf)
		self_conf = "no";

	old_timeout = dut->default_timeout;
	val = get_param(cmd, "DPPTimeout");
	if (val && atoi(val) > 0) {
		dut->default_timeout = atoi(val);
		sigma_dut_print(dut, DUT_MSG_DEBUG, "DPP timeout: %u",
				dut->default_timeout);
	}

	if (strcasecmp(bs, "NFC") == 0) {
		res = dpp_automatic_dpp(dut, conn, cmd);
		goto out;
	}

	res = dpp_get_local_bootstrap(dut, conn, cmd, 0, &success);
	if (res != STATUS_SENT || !success)
		goto out;

	if (!auth_role) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing DPPAuthRole");
		return STATUS_SENT_ERROR;
	}

	if (strcasecmp(auth_role, "Responder") == 0) {
		if (dpp_display_own_qrcode(dut) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to display own QR code");
			res = STATUS_SENT_ERROR;
			goto out;
		}

		res = dpp_automatic_dpp(dut, conn, cmd);
		goto out;
	}

	if (strcasecmp(auth_role, "Initiator") == 0) {
		if (strcasecmp(self_conf, "Yes") != 0) {
			if (dpp_scan_peer_qrcode(dut) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to scan peer QR Code");
				res = STATUS_SENT_ERROR;
				goto out;
			}
		}

		res = dpp_automatic_dpp(dut, conn, cmd);
		goto out;
	}

	send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unknown DPPAuthRole");
	res = STATUS_SENT_ERROR;
out:
	dut->default_timeout = old_timeout;
	return res;
}


static enum sigma_cmd_result
dpp_reconfigure_configurator(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	const char *val;
	int freq;
	struct wpa_ctrl *ctrl = NULL;
	const char *ifname;
	int conf_index;
	const char *conf_role;
	const char *group_id_str = NULL;
	char *pos;
	char buf[2000];
	char buf2[200];
	char conf_ssid[100];
	char conf_pass[100];
	char csrattrs[200];
	char group_id[100];
	char conf2[300];
	FILE *f;
	int enrollee_ap = 0;
	int force_gas_fragm = 0;
	int akm_use_selector = 0;
	int conn_status;
	int res;
	const char *conf_events[] = {
		"DPP-CONF-SENT",
		"DPP-CONF-FAILED",
		NULL
	};

	if (sigma_dut_is_ap(dut)) {
		if (!dut->hostapd_ifname) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"hostapd ifname not specified (-j)");
			return ERROR_SEND_STATUS;
		}
		ifname = dut->hostapd_ifname;
	} else {
		ifname = get_station_ifname(dut);
	}

	val = get_param(cmd, "DPPConfEnrolleeRole");
	if (val)
		enrollee_ap = strcasecmp(val, "AP") == 0;

	val = get_param(cmd, "DPPStatusQuery");
	conn_status = val && strcasecmp(val, "Yes") == 0;

	conf_ssid[0] = '\0';
	conf_pass[0] = '\0';
	csrattrs[0] = '\0';
	group_id[0] = '\0';
	conf2[0] = '\0';

	val = get_param(cmd, "DPPConfIndex");
	if (!val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"DPPConfIndex not specified for Configurator");
		return ERROR_SEND_STATUS;
	}
	conf_index = atoi(val);

	switch (conf_index) {
	case 1:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		if (enrollee_ap) {
			conf_role = "ap-dpp";
		} else {
			conf_role = "sta-dpp";
		}
		group_id_str = "DPPGROUP_DPP_INFRA";
		break;
	case 2:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		snprintf(conf_pass, sizeof(conf_pass),
			 "psk=10506e102ad1e7f95112f6b127675bb8344dacacea60403f3fa4055aec85b0fc");
		if (enrollee_ap)
			conf_role = "ap-psk";
		else
			conf_role = "sta-psk";
		break;
	case 3:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		ascii2hexstr("ThisIsDppPassphrase", buf);
		res = snprintf(conf_pass, sizeof(conf_pass), "pass=%s", buf);
		if (res < 0 || res >= sizeof(conf_pass))
			goto err;
		if (enrollee_ap)
			conf_role = "ap-psk";
		else
			conf_role = "sta-psk";
		break;
	case 4:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		if (enrollee_ap) {
			conf_role = "ap-dpp";
		} else {
			conf_role = "sta-dpp";
		}
		group_id_str = "DPPGROUP_DPP_INFRA2";
		break;
	case 5:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		ascii2hexstr("ThisIsDppPassphrase", buf);
		res = snprintf(conf_pass, sizeof(conf_pass), "pass=%s", buf);
		if (res < 0 || res >= sizeof(conf_pass))
			goto err;
		if (enrollee_ap)
			conf_role = "ap-sae";
		else
			conf_role = "sta-sae";
		break;
	case 6:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		ascii2hexstr("ThisIsDppPassphrase", buf);
		res = snprintf(conf_pass, sizeof(conf_pass), "pass=%s", buf);
		if (res < 0 || res >= sizeof(conf_pass))
			goto err;
		if (enrollee_ap)
			conf_role = "ap-psk-sae";
		else
			conf_role = "sta-psk-sae";
		break;
	case 7:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		if (enrollee_ap) {
			conf_role = "ap-dpp";
		} else {
			conf_role = "sta-dpp";
		}
		group_id_str = "DPPGROUP_DPP_INFRA";
		force_gas_fragm = 1;
		break;
	case 8:
	case 9:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		ascii2hexstr("This_is_legacy_password", buf);
		res = snprintf(conf_pass, sizeof(conf_pass), "pass=%s", buf);
		if (res < 0 || res >= sizeof(conf_pass))
			goto err;
		if (enrollee_ap) {
			conf_role = "ap-dpp+psk+sae";
		} else {
			conf_role = "sta-dpp+psk+sae";
		}
		group_id_str = "DPPGROUP_DPP_INFRA1";
		if (conf_index == 9)
			akm_use_selector = 1;
		break;
	case 10:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		if (enrollee_ap)
			conf_role = "ap-dpp";
		else
			conf_role = "sta-dpp";
		group_id_str = "DPPGROUP_DPP_INFRA1";
		ascii2hexstr("DPPNET02", buf);
		ascii2hexstr("This_is_legacy_password", buf2);
		res = snprintf(conf2, sizeof(conf2),
			       " @CONF-OBJ-SEP@ conf=%s-dpp+psk+sae ssid=%s pass=%s group_id=DPPGROUP_DPP_INFRA2",
			       enrollee_ap ? "ap" : "sta", buf, buf2);
		if (res < 0 || res >= sizeof(conf2))
			goto err;
		break;
	case 11:
		ascii2hexstr("DPPNET01", buf);
		res = snprintf(conf_ssid, sizeof(conf_ssid), "ssid=%s", buf);
		if (res < 0 || res >= sizeof(conf_ssid))
			goto err;
		if (enrollee_ap) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,dot1x AKM provisioning not supported for AP");
			goto out;
		}
		conf_role = "sta-dot1x";
		snprintf(buf, sizeof(buf), "%s/dpp-ca-csrattrs",
			 sigma_cert_path);
		f = fopen(buf, "r");
		if (f) {
			size_t len;
			int r;

			len = fread(buf, 1, sizeof(buf), f);
			fclose(f);
			if (len >= sizeof(buf)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,No room for csrAttrs");
				goto out;
			}
			buf[len] = '\0';
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Use csrAttrs from file");
			r = snprintf(csrattrs, sizeof(csrattrs),
				     " csrattrs=%s", buf);
			if (r <= 0 || r >= sizeof(csrattrs)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,No room for csrAttrs");
				goto out;
			}
		} else {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Use default csrAttrs");
			snprintf(csrattrs, sizeof(csrattrs), "%s",
				 " csrattrs=MAsGCSqGSIb3DQEJBw==");
		}
		break;
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported DPPConfIndex");
		goto out;
	}

	if (group_id_str)
		snprintf(group_id, sizeof(group_id), " group_id=%s",
			 group_id_str);

	if (force_gas_fragm) {
		char spaces[1500];

		memset(spaces, ' ', sizeof(spaces));
		spaces[sizeof(spaces) - 1] = '\0';

		snprintf(buf, sizeof(buf),
			 "SET dpp_discovery_override {\"ssid\":\"DPPNET01\"}%s",
			 spaces);
		if (wpa_command(ifname, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set discovery override");
			goto out;
		}
	}

	snprintf(buf, sizeof(buf),
		 "SET dpp_configurator_params  conf=%s %s %s configurator=%d%s%s%s%s%s",
		 conf_role, conf_ssid, conf_pass,
		 dut->dpp_conf_id, group_id,
		 akm_use_selector ? " akm_use_selector=1" : "",
		 conn_status ? " conn_status=1" : "", csrattrs,
		 conf2);
	if (wpa_command(ifname, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set configurator parameters");
		goto out;
	}

	ctrl = open_wpa_mon(ifname);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return ERROR_SEND_STATUS;
	}

	val = get_param(cmd, "DPPListenChannel");
	if (val) {
		freq = channel_to_freq(dut, atoi(val));
		if (freq == 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported DPPListenChannel value");
			goto out;
		}
		snprintf(buf, sizeof(buf),
			 "DPP_LISTEN %d role=configurator", freq);
		if (wpa_command(ifname, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Could not start listen state");
			goto out;
		}
	}

	res = get_wpa_cli_event(dut, ctrl, "DPP-CONF-REQ-RX",
				buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "ReconfigAuthResult,Timeout");
		goto out;
	}

	res = get_wpa_cli_events(dut, ctrl, conf_events, buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "ReconfigAuthResult,OK,ConfResult,Timeout");
		goto out;
	}
	if (!strstr(buf, "DPP-CONF-SENT")) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "ReconfigAuthResult,OK,ConfResult,FAILED");
		goto out;
	}

	if (conn_status && strstr(buf, "wait_conn_status=1")) {
		res = get_wpa_cli_event(dut, ctrl, "DPP-CONN-STATUS-RESULT",
					buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "ReconfigAuthResult,OK,ConfResult,OK,StatusResult,Timeout");
		} else {
			pos = strstr(buf, "result=");
			if (!pos) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Status result value not reported");
			} else {
				pos += 7;
				snprintf(buf, sizeof(buf),
					 "ReconfigAuthResult,OK,ConfResult,OK,StatusResult,%d",
					 atoi(pos));
				send_resp(dut, conn, SIGMA_COMPLETE, buf);
			}
		}
		goto out;
	}

	send_resp(dut, conn, SIGMA_COMPLETE,
		  "ReconfigAuthResult,OK,ConfResult,OK");

out:
	if (ctrl) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
	}
	return STATUS_SENT;
err:
	send_resp(dut, conn, SIGMA_ERROR, NULL);
	goto out;
}


static enum sigma_cmd_result dpp_reconfigure(struct sigma_dut *dut,
					     struct sigma_conn *conn,
					     struct sigma_cmd *cmd)
{
	const char *wait_conn;
	char buf[200];
	char *pos;
	const char *ifname;
	struct wpa_ctrl *ctrl;
	const char *conf_events[] = {
		"DPP-CONF-RECEIVED",
		"DPP-CONF-FAILED",
		NULL
	};
	const char *conn_events[] = {
		"PMKSA-CACHE-ADDED",
		"CTRL-EVENT-CONNECTED",
		NULL
	};
	int res;

	if (get_param(cmd, "DPPConfIndex"))
		return dpp_reconfigure_configurator(dut, conn, cmd);

	/* Enrollee reconfiguration steps */
	ifname = get_station_ifname(dut);
	wait_conn = get_param(cmd, "DPPWaitForConnect");
	if (!wait_conn)
		wait_conn = "no";

	ctrl = open_wpa_mon(ifname);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return ERROR_SEND_STATUS;
	}

	snprintf(buf, sizeof(buf), "DPP_RECONFIG %d iter=10",
		 dut->dpp_network_id);
	if (wpa_command(ifname, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to start reconfiguration");
		goto out;
	}

	res = get_wpa_cli_event(dut, ctrl, "GAS-QUERY-START",
				buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "ReconfigAuthResult,Timeout");
		goto out;
	}

	res = get_wpa_cli_events(dut, ctrl, conf_events, buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "ReconfigAuthResult,OK,ConfResult,Timeout");
		goto out;
	}
	if (!strstr(buf, "DPP-CONF-RECEIVED")) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "ReconfigAuthResult,OK,ConfResult,FAILED");
		goto out;
	}

	res = get_wpa_cli_event(dut, ctrl, "DPP-NETWORK-ID", buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,No DPP-NETWORK-ID");
		goto out;
	}
	pos = strchr(buf, ' ');
	if (!pos) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Invalid DPP-NETWORK-ID");
		goto out;
	}
	pos++;
	dut->dpp_network_id = atoi(pos);
	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Network ID for the reconfigured network: %d",
			dut->dpp_network_id);

	if (strcasecmp(wait_conn, "Yes") == 0) {
		int not_dpp_akm = 0;

		snprintf(buf, sizeof(buf), "GET_NETWORK %d key_mgmt",
			 dut->dpp_network_id);
		if (wpa_command_resp(ifname, buf, buf, sizeof(buf)) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Could not fetch provisioned key_mgmt");
			goto out;
		}
		if (strncmp(buf, "SAE", 3) == 0) {
			/* SAE generates PMKSA-CACHE-ADDED event */
			not_dpp_akm = 1;
		}

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
					  not_dpp_akm ?
					  "ReconfigAuthResult,OK,ConfResult,OK,NetworkConnectResult,Timeout" :
					  "ReconfigAuthResult,OK,ConfResult,OK,NetworkIntroResult,OK,NetworkConnectResult,Timeout");
				goto out;
			}
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"DPP connect result: %s", buf);
			if (strstr(buf, "CTRL-EVENT-CONNECTED"))
				send_resp(dut, conn, SIGMA_COMPLETE,
					  not_dpp_akm ?
					  "ReconfigAuthResult,OK,ConfResult,OK,NetworkConnectResult,OK" :
					  "ReconfigAuthResult,OK,ConfResult,OK,NetworkIntroResult,OK,NetworkConnectResult,OK");
			else
				send_resp(dut, conn, SIGMA_COMPLETE,
					  not_dpp_akm ?
					  "ReconfigAuthResult,OK,ConfResult,OK,NetworkConnectResult,Timeout" :
					  "ReconfigAuthResult,OK,ConfResult,OK,NetworkIntroResult,OK,NetworkConnectResult,Timeout");
			goto out;
		}

		send_resp(dut, conn, SIGMA_COMPLETE,
			  "ReconfigAuthResult,OK,ConfResult,OK,NetworkConnectResult,OK");
		goto out;
	}

	send_resp(dut, conn, SIGMA_COMPLETE,
		  "ReconfigAuthResult,OK,ConfResult,OK");

out:
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);
	return STATUS_SENT;
}


enum sigma_cmd_result dpp_dev_exec_action(struct sigma_dut *dut,
					  struct sigma_conn *conn,
					  struct sigma_cmd *cmd)
{
	const char *type = get_param(cmd, "DPPActionType");
	const char *bs = get_param(cmd, "DPPBS");

	if (!type) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing DPPActionType");
		return STATUS_SENT_ERROR;
	}

	if (strcasecmp(type, "DPPReconfigure") == 0)
		return dpp_reconfigure(dut, conn, cmd);

	if (!bs) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing DPPBS");
		return STATUS_SENT_ERROR;
	}

	if (strcasecmp(type, "GetLocalBootstrap") == 0)
		return dpp_get_local_bootstrap(dut, conn, cmd, 1, NULL);
	if (strcasecmp(type, "SetPeerBootstrap") == 0)
		return dpp_set_peer_bootstrap(dut, conn, cmd);
	if (strcasecmp(type, "ManualDPP") == 0)
		return dpp_manual_dpp(dut, conn, cmd);
	if (strcasecmp(type, "AutomaticDPP") == 0)
		return dpp_automatic_dpp(dut, conn, cmd);

	send_resp(dut, conn, SIGMA_ERROR,
		  "errorCode,Unsupported DPPActionType");
	return STATUS_SENT_ERROR;
}
