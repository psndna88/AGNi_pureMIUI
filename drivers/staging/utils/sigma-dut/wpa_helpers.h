/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 * Copyright (c) 2012-2014, 2016, Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#ifndef WPA_HELPERS_H
#define WPA_HELPERS_H

const char * get_main_ifname(struct sigma_dut *dut);
const char * get_station_ifname(struct sigma_dut *dut);
const char * get_p2p_ifname(struct sigma_dut *dut, const char *primary_ifname);
void dut_ifc_reset(struct sigma_dut *dut);

int wpa_command(const char *ifname, const char *cmd);
int hapd_command(const char *ifname, const char *cmd);
int wpa_command_resp(const char *ifname, const char *cmd,
		     char *resp, size_t resp_size);
int hapd_command_resp(const char *ifname, const char *cmd,
		      char *resp, size_t resp_size);
int get_wpa_status(const char *ifname, const char *field, char *obuf,
		   size_t obuf_size);
int get_wpa_signal_poll(struct sigma_dut *dut, const char *ifname,
			const char *field, char *obuf, size_t obuf_size);
int get_wpa_ssid_bssid(struct sigma_dut *dut, const char *ifname,
		       char *buf, size_t buf_size);
int get_hapd_config(const char *ifname, const char *field, char *obuf,
		    size_t obuf_size);
void remove_wpa_networks(const char *ifname);

struct wpa_ctrl * open_wpa_mon(const char *ifname);
struct wpa_ctrl * open_hapd_mon(const char *ifname);
int wait_ip_addr(struct sigma_dut *dut, const char *ifname, int timeout);
int get_wpa_cli_event(struct sigma_dut *dut, struct wpa_ctrl *mon,
		      const char *event, char *buf, size_t buf_size);
int get_wpa_cli_event2(struct sigma_dut *dut, struct wpa_ctrl *mon,
		       const char *event, const char *event2,
		       char *buf, size_t buf_size);
int get_wpa_cli_events(struct sigma_dut *dut, struct wpa_ctrl *mon,
		       const char **events, char *buf, size_t buf_size);

int add_network(const char *ifname);
int set_network(const char *ifname, int id, const char *field,
		const char *value);
int set_network_quoted(const char *ifname, int id, const char *field,
		       const char *value);
int add_cred(const char *ifname);
int set_cred(const char *ifname, int id, const char *field, const char *value);
int set_cred_quoted(const char *ifname, int id, const char *field,
		    const char *value);

const char * concat_sigma_tmpdir(struct sigma_dut *dut, const char *src,
				 char *dst, size_t len);

int start_sta_mode(struct sigma_dut *dut);
void stop_sta_mode(struct sigma_dut *dut);

#endif /* WPA_HELPERS_H */
