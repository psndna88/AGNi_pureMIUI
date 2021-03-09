/*
 * Sigma Control API DUT - Miracast interface
 * Copyright (c) 2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2019, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 *
 * Implementation of MIRACAST specific functionality.
 * For example, starting wfd session.
*/

#include "sigma_dut.h"
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include "wpa_ctrl.h"
#include "wpa_helpers.h"
#include "miracast.h"
#ifdef ANDROID
#include "properties.h"
#include <netutils/ifc.h>
#endif /* ANDROID */

#define HUNDRED_SECOND_TIMEOUT   100 /* 100 seconds */
#define DHCP_LEASE_FILE_PATH "/data/misc/dhcp/dnsmasq.leases"
#define MIRACAST_CMD_LEN         512

static int session_management_control_port = 7236;
/* Followingng stores p2p interface name after P2P group formation */
static char wfd_ifname[32];

extern void get_dhcp_info(uint32_t *ipaddr, uint32_t *gateway,
			  uint32_t *prefixLength, uint32_t *dns1,
			  uint32_t *dns2, uint32_t *server,
			  uint32_t *lease);

extern int do_dhcp(char *);

const char *ipaddr (in_addr_t addr)
{
	struct in_addr in_addr;
	in_addr.s_addr = addr;
	return inet_ntoa(in_addr);
}




static int miracast_load(struct sigma_dut *dut)
{
	static int once = 1;

	if (!once)
		return 0;

	once = 0;
	dlerror();
	dut->miracast_lib = dlopen(dut->miracast_lib_path ?
				   dut->miracast_lib_path : "libmiracast.so",
				   RTLD_LAZY);
	if (!dut->miracast_lib) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Fail to load Miracast library %s",
				dlerror());
		return -1;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"Miracast Wi-Fi Display library found - starting service");
	return 0;
}


static int miracast_unload(struct sigma_dut *dut)
{
	int err;

	if (!dut->miracast_lib)
		return -1;

	dlerror();
	sigma_dut_print(dut, DUT_MSG_INFO, "Unloading Miracast library");
	err = dlclose(dut->miracast_lib);
	dut->miracast_lib = NULL;
	if (err == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
			 "Miracast library successfully unloaded");
	} else {
		sigma_dut_print(dut, DUT_MSG_INFO,
			"Failed to unload Miracast library");
	}
	return err;
}


static void get_modified_peer_mac_address(struct sigma_dut *dut)
{
	struct wpa_ctrl *ctrl;
	char event_buf[64];
	char *peer;
	int res;

	dut->modified_peer_mac_address[0] = '\0';
	ctrl = open_wpa_mon(wfd_ifname); /* Refer to wfd_ifname */
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return;
	}
	res = get_wpa_cli_event(dut, ctrl, "AP-STA-CONNECTED",
				event_buf, sizeof(event_buf));
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Could not get event before timeout");
		return;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "STA connected event: '%s'",
			event_buf);
	peer = strchr(event_buf, ' ');
	if (!peer) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Could not find STA MAC address");
		return;
	}

	peer++;
	strlcpy(dut->modified_peer_mac_address, peer,
		sizeof(dut->modified_peer_mac_address));
}


static int get_peer_ip_p2p_go(struct sigma_dut *dut, char *ipaddr,
			      unsigned int wait_limit)
{

	FILE *fp;
	char *macaddr;

	if (dut->modified_peer_mac_address[0])
		macaddr = dut->modified_peer_mac_address;
	else
		macaddr = dut->peer_mac_address;

	fp = fopen(DHCP_LEASE_FILE_PATH, "r");
	if (!fp) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Could not open DHCP lease file");
		return -1;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "macaddress %s", macaddr);
	while (wait_limit > 0) {
		char line[100] = { 0 };
		char *str1 = NULL;
		char *dummy_str = NULL;
		char dummy_macaddress[32];
		int ip_found = 0;
		int len;

		fseek(fp, 0, SEEK_SET);
		while (fgets(line, sizeof(line), fp) != NULL) {
			len = strlen(line);
			if (len == 0)
				continue;

			str1 = strtok_r(line, " ", &dummy_str);
			if (str1 == NULL)
				break;

			/* Look for mac address */
			str1 = strtok_r(NULL, " ", &dummy_str);
			if (str1 == NULL)
				break;

			strlcpy(dummy_macaddress, str1,
				sizeof(dummy_macaddress));

			/* Look for ip address */
			str1 = strtok_r(NULL, " ", &dummy_str);
			if (str1 == NULL)
				break;

			strlcpy(ipaddr,str1,32);

			sigma_dut_print(dut, DUT_MSG_INFO,
					"Peer IP Address obtained and mac %s %s",
					ipaddr, dummy_macaddress);

			/* Match all the octets of MAC address */
			if (strncasecmp(macaddr, dummy_macaddress, 17) == 0) {
				ip_found = 1;
				sigma_dut_print(dut, DUT_MSG_INFO,
						"Obtained the IP address %s",
						ipaddr);
				break;
			}
		}

		if (ip_found)
			break;

		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to find IP from DHCP lease file");
		sleep(1);
		wait_limit--;
	}
	fclose(fp);
	return 0;
}


static int miracast_start_dhcp_client(struct sigma_dut *dut, const char *ifname)
{
	int ret = ifc_init();

	sigma_dut_print(dut, DUT_MSG_DEBUG, "ifc init returned %d", ret);
	ret = do_dhcp((char *) ifname);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "do dhcp returned %d", ret);
	return 0;
}


static void miracast_stop_dhcp_client(struct sigma_dut *dut, const char *ifname)
{
	ifc_close();
}


static int get_peer_ip_p2p_client(struct sigma_dut *dut, char *ipAddr,
				  const char *intf, unsigned int wait_limit)
{
	uint32_t ipaddress, gateway, prefixLength,
		dns1, dns2, serveraddr, lease;

	get_dhcp_info(&ipaddress, &gateway, &prefixLength, &dns1, &dns2,
		      &serveraddr, &lease);
	while (wait_limit > 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Peer IP: %u", ipaddress);
		if (strlen(ipaddr(serveraddr)) > 8) {
			/* connected */
			strlcpy(ipAddr, ipaddr(serveraddr), 16);
			break;
		}
		sleep(1);
		wait_limit--;
	}
	return wait_limit == 0 ? -1 : 0;
}


static int get_p2p_connection_event(struct sigma_dut *dut,
				    const char *input_intf,
				    char *output_intf,
				    int size_output_intf,
				    int *is_group_owner)
{
	/*
	* Poll for the P2P Connection
	* Then poll for IP
	* Then poll for WFD session ID and exit
	* P2P connection done
	* Loop till connection is ready
	*/
	struct wpa_ctrl *ctrl;
	char *mode_string;
	char event_buf[256];
	char *ifname;
	char *pos;
	int res = 0;
	const char *events[] = {
		"P2P-GROUP-STARTED",
		"P2P-GO-NEG-FAILURE",
		"P2P-GROUP-FORMATION-FAILURE",
		NULL
	};

	/* Wait for WPA CLI EVENTS */
	/* Default timeout is 120s */
	ctrl = open_wpa_mon(input_intf);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return -1;
	}

	res = get_wpa_cli_events(dut, ctrl, events, event_buf,
				 sizeof(event_buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Group formation did not complete");
		return -1;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Received event %s", event_buf);

	if (strstr(event_buf, "P2P-GROUP-FORMATION-FAILURE") ||
	    strstr(event_buf, "P2P-GO-NEG-FAILURE"))
		return -1;

	sigma_dut_print(dut, DUT_MSG_INFO, "P2P connection done");
	ifname = strchr(event_buf, ' ');
	if (!ifname) {
		sigma_dut_print(dut, DUT_MSG_INFO, "No P2P interface found");
		return -1;
	}
	ifname++;
	pos = strchr(ifname, ' ');
	if (!pos) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "No P2P interface found");
		return -1;
	}
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group interface %s", ifname);

	strlcpy(output_intf, ifname, size_output_intf);

	mode_string = pos;
	pos = strchr(mode_string, ' ');
	if (!pos) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "No group role found");
		return -1;
	}

	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group Role %s", mode_string);

	if (strcmp(mode_string, "GO") == 0) {
		*is_group_owner = 1;
		get_modified_peer_mac_address(dut);
	}
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Value of is_group_owner %d",
			*is_group_owner);
	return 0;
}


/* Following serves as an entry point function to perform rtsp tasks */
static void * miracast_rtsp_thread_entry(void *ptr)
{
	struct sigma_dut *dut = ptr;
	char output_ifname[16];
	int is_group_owner = 0;
	const char *intf = dut->station_ifname;
	unsigned int wait_limit;
	char peer_ip_address[32];
	int rtsp_session_id = -1;
	int (*extn_start_wfd_connection)(const char *,
					 const char *, /* Peer IP */
					 int, /* RTSP port number */
					 int, /* WFD Device Type; 0-Source,
						 1-P-Sink, 2-Secondary Sink */
					 int *); /* for returning session ID */

	miracast_load(dut);

	if (dut->main_ifname) {
		intf = get_main_ifname(dut);
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"miracast_rtsp_thread_entry: sigma_main_ifname = [%s]",
				intf);
	} else {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"miracast_rtsp_thread_entry: sigma_main_ifname is NULL");
	}

	if (get_p2p_connection_event(dut, intf, output_ifname,
				     sizeof(output_ifname),
				     &is_group_owner) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "P2P connection failure");
		goto EXIT;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Waiting to start dhcp");

	/* Calling WFD APIs now */
	/* If you are a source, go ahead and start the RTSP server */
	if (dut->wfd_device_type != 0)
		wait_limit = HUNDRED_SECOND_TIMEOUT;
	else
		wait_limit = 500;

	if (!is_group_owner) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Waiting to start dhcp client");
		sleep(5); /* Wait for IP */
		miracast_start_dhcp_client(dut, output_ifname);
		sleep(5); /* Wait for IP */
		if (get_peer_ip_p2p_client(dut, peer_ip_address, output_ifname,
					   wait_limit) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Could not get peer IP");
			goto EXIT;
		}
	} else {
		stop_dhcp(dut, output_ifname, 1);
		/* For GO read the DHCP Lease File */
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Waiting to start dhcp server");
		start_dhcp(dut, output_ifname, 1);
		sleep(5);
		if (get_peer_ip_p2p_go(dut, peer_ip_address, wait_limit) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Could not get peer IP");
			goto EXIT;
		}
	}

	extn_start_wfd_connection = dlsym(dut->miracast_lib,
					  "start_wfd_connection");
	if (extn_start_wfd_connection) {
		extn_start_wfd_connection(NULL, peer_ip_address,
					  session_management_control_port,
					  1 - dut->wfd_device_type,
					  &rtsp_session_id);
	} else {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"dlsym seems to have error %p %p",
				dut->miracast_lib, extn_start_wfd_connection);
	}

EXIT:
	sigma_dut_print(dut, DUT_MSG_INFO, "Reached Miracast thread exit");

	return NULL;
}


/*----------------------------------------------------------------------
  WFD Source IE: 000601101c440036
  len        WFD device info          control port              throughput
  110]    [00000 00100010 000]    [00011 10001000 100]    [00000 00000110 110]
				       = 7236

  WFD Sink IE: 000601511c440036
  len        WFD device info          control port              throughput
  110]    [00000 00101010 001]    [00011 10001000 100]    [00000 00000110 110]
				       = 7236

  WFD device info:
  BITS        NAME                DESCRIPTION
  -------------------------------------------
  1:0     WFD Device Type     0b00: WFD Source
			      0b01: Primary Sink
			      0b10: Secondary Sink
			      0b11: Dual Role, either WFD Source/Primary sink

  5:4     WFD Session         0b00: Not available for WFD Session
			Availibility	0b01: Available for WFD Session
							0b10, 0b11: Reserved

  6       WSD Support Bit     0b0: WFD Service Discovery not supported
			      0b1: WFD Service Discovery supported

  8       CP Support Bit      0b0: Content Protection via HDCP not supported
			      0b1: Content Protection via HDCP supported
---------------------------------------------------------------------------
*/

static void miracast_set_wfd_ie(struct sigma_dut *sigma_dut)
{
	const char *intf = sigma_dut->station_ifname;

	if (sigma_dut->main_ifname != NULL)
		intf = get_main_ifname(sigma_dut);

	sigma_dut_print(sigma_dut, DUT_MSG_DEBUG, "miracast_set_wfd_ie() = intf = %s",
			intf);
	wpa_command(intf, "SET wifi_display 1");

	if (sigma_dut->wfd_device_type == 0) {
		wpa_command(intf, "WFD_SUBELEM_SET 0 000601101c440036");
		wpa_command(intf, "WFD_SUBELEM_SET 11 00020000");
	} else {
		wpa_command(intf, "WFD_SUBELEM_SET 0 000601511c440036");
		wpa_command(intf, "WFD_SUBELEM_SET 11 00020001");
	}
}


void miracast_init(struct sigma_dut *dut)
{
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Create thread pool for VDS");
	miracast_set_wfd_ie(dut);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Clear groupID @ start");
}


void miracast_deinit(struct sigma_dut *dut)
{
	(void) miracast_unload(dut);
}


static void miracast_generate_string_cmd(struct sigma_cmd *cmd, char *strcmd,
					 size_t size)
{
	int i = 0;
	char *pos, *end;
	int ret;

	if (!strcmd)
		return;
	strcmd[0] = '\0';
	pos = strcmd;
	end = strcmd + size;
	for (i = 0; i < cmd->count; i++) {
		ret = snprintf(pos, end - pos, "%s,%s,", cmd->params[i],
			       cmd->values[i]);
		if (ret < 0 || ret >= end - pos)
			break;
		pos += ret;
	}

	pos = strrchr(strcmd, ',');
	if (pos)
		*pos = '\0';
	printf("Miracast: generated command: %s\n", strcmd);
}


static void * auto_go_thread_entry(void *ptr)
{
	struct sigma_dut *dut = ptr;
	struct wpa_ctrl *ctrl;
	char event_buf[64];
	char *peer = NULL;
	int res = 0;
	char macaddress[32];
	char peer_ip_address[32];
	int rtsp_session_id = -1;
	int (*extn_start_wfd_connection)(const char *,
					 const char *, /* Peer IP */
					 int, /* RTSP port number */
					 int, /* WFD Device Type; 0-Source,
						1-P-Sink, 2-Secondary Sink */
					 int *); /* for returning session ID */

	stop_dhcp(dut, wfd_ifname, 1);
	/* For auto-GO, start the DHCP server and wait for 5 seconds */
	start_dhcp(dut, wfd_ifname, 1);
	sleep(5); /* Wait for IP */

	sigma_dut_print(dut, DUT_MSG_INFO, "Wait for AP-STA-CONNECTED");
	ctrl = open_wpa_mon(wfd_ifname); /* Refer to wfd_ifname */
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		goto THR_EXIT;
	}
	res = get_wpa_cli_event(dut, ctrl, "AP-STA-CONNECTED",
				event_buf, sizeof(event_buf));
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Could not get event before timeout");
		goto THR_EXIT;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "STA Connected Event: '%s'",
			event_buf);
	peer = strchr(event_buf, ' ');
	if (!peer) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Could not find STA MAC");
		goto THR_EXIT;
	}

	peer++;
	strlcpy(macaddress, peer, sizeof(macaddress));
	if (get_peer_ip_p2p_go(dut, peer_ip_address, 30) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Could not get peer IP");
		goto THR_EXIT;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "dlsym %p", dut->miracast_lib);
	extn_start_wfd_connection = dlsym(dut->miracast_lib,
					  "start_wfd_connection");
	if (!extn_start_wfd_connection)
		sigma_dut_print(dut, DUT_MSG_INFO, "dlsym function NULL");
	else
		extn_start_wfd_connection(NULL, peer_ip_address,
					  session_management_control_port,
					  1 - dut->wfd_device_type,
					  &rtsp_session_id);

THR_EXIT:
	sigma_dut_print(dut, DUT_MSG_INFO, "Reached auto GO thread exit");
	return NULL;
}


void miracast_sta_reset_default(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *intf = dut->station_ifname;
	int (*extn_sta_reset_default)(const char *);
	char string_cmd[MIRACAST_CMD_LEN] = { 0 };

	if (dut->main_ifname != NULL)
		intf = get_main_ifname(dut);
	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"miracast_sta_reset_default() = intf = %s", intf);
	stop_dhcp(dut, intf, 1); /* IFNAME argument is ignored */
	miracast_stop_dhcp_client(dut, intf);

	/* This is where vendor Miracast library is loaded and function pointers
	 * to Miracast functions (defined by CAPI) are loaded. */

	if (miracast_load(dut) != 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Fail to load Miracast library");
		return;
	}

	if (!dut->miracast_lib) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Miracast library is absent");
		return;
	}

	dlerror();

	miracast_generate_string_cmd(cmd, string_cmd, sizeof(string_cmd));
	extn_sta_reset_default = dlsym(dut->miracast_lib, "sta_reset_default");
	if (extn_sta_reset_default)
		extn_sta_reset_default(string_cmd);

	/* delete threads if any */
	/* TODO: if dut->rtsp_thread_handle running, call
	 * miracast_release_rtsp_thread_resources(dut); */
}


void miracast_start_autonomous_go(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd, char *ifname)
{
	strlcpy(wfd_ifname, ifname, sizeof(wfd_ifname));
	(void) pthread_create(&dut->rtsp_thread_handle, NULL,
			      auto_go_thread_entry, dut);
}


static void miracast_rtsp_thread_create(struct sigma_dut *dut,
					struct sigma_conn *conn,
					struct sigma_cmd *cmd)
{
	(void) pthread_create(&dut->rtsp_thread_handle, NULL,
			      miracast_rtsp_thread_entry, dut);
}


int miracast_dev_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	const char *frame_name = get_param(cmd, "FrameName");
	/* const char *source = get_param(cmd, "Source"); */
	/* const char *destination = get_param(cmd, "Destination"); */
	/* const char *dev_type = get_param(cmd, "DevType"); */
	const char *rtsp_msg_type = get_param(cmd, "RtspMsgType");
	/* const char *wfd_session_id = get_param(cmd, "WfdSessionID"); */
	int (*dev_send_frame)(const char *);
	char string_cmd[MIRACAST_CMD_LEN] = { 0 };

	dev_send_frame = dlsym(dut->miracast_lib, "dev_send_frame");
	if (!dev_send_frame)
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "miracast_dev_send_frame 1");
	miracast_generate_string_cmd(cmd, string_cmd, sizeof(string_cmd));
	if (!frame_name)
		return 0;
	if (strcasecmp(frame_name, "RTSP") != 0)
		return 0;

	if (!rtsp_msg_type)
		return 0;

	if (strcasecmp(rtsp_msg_type, "PAUSE") == 0 ||
	    strcasecmp(rtsp_msg_type, "TRIGGER-PAUSE") == 0) {
		/* Call RTSP Pause */
		dev_send_frame(string_cmd);
		return 1;
	}

	if (strcasecmp(rtsp_msg_type, "PLAY") == 0 ||
	    strcasecmp(rtsp_msg_type, "TRIGGER-PLAY") == 0) {
		/* Call RTSP Play */;
		dev_send_frame(string_cmd); /* Not for secure playback */
		return 1;
	}

	if (strcasecmp(rtsp_msg_type, "TEARDOWN") == 0 ||
	    strcasecmp(rtsp_msg_type, "TRIGGER-TEARDOWN") == 0) {
		dev_send_frame(string_cmd); /* RTSP Teardown */
		return 1;
	}

	if (strcasecmp(rtsp_msg_type,"SET_PARAMETER") == 0) {
		const char *set_parameter = get_param(cmd, "SetParameter");
		const char *transportType = get_param(cmd, "TransportType");

		if (set_parameter == NULL && transportType == NULL) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid Set Parameter value");
			return 0;
		}

		if (1) /* (strcasecmp(set_parameter, "Standby") == 0) */ {
			dev_send_frame(string_cmd);
			return 1;
		}
		/* TODO More needs to be implemented when the spec is clearer */
		return 1;
	}

	if (strcasecmp(rtsp_msg_type, "SETUP") == 0) {
		dev_send_frame(string_cmd);
		/* TODO More needs to be implemented when the spec is clearer */
		return 1;
	}

	if (strcasecmp(frame_name, "WFD_ProbeReq") == 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported WFD Probe Request");
		return 0;
	}

	if (strcasecmp(frame_name, "WFD_ServiceDiscReq") == 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported WFD Service Discovery");
		return 0;
	}

	send_resp(dut, conn, SIGMA_ERROR,
		  "errorCode,Unsupported dev_send_frame");
	return 0;
}


int miracast_dev_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	const char *service_type = get_param(cmd,"ServiceType");
	int (*dev_exec_action)(const char *);
	char string_cmd[MIRACAST_CMD_LEN] = { 0 };

	sigma_dut_print(dut, DUT_MSG_DEBUG, "miracast_dev_exec_frame");

	if (service_type) {
		char resp_buf[128];

		sigma_dut_print(dut, DUT_MSG_DEBUG, "MDNS Instance Name = %s",
				dut->mdns_instance_name);
		strlcpy(resp_buf, "InstanceName,", sizeof(resp_buf));
		strlcat(resp_buf + strlen(resp_buf), dut->mdns_instance_name,
			sizeof(resp_buf) - strlen(resp_buf));
		send_resp(dut, conn, SIGMA_COMPLETE, resp_buf);
		return 0;
	}

	miracast_generate_string_cmd(cmd, string_cmd, sizeof(string_cmd));
	dev_exec_action = dlsym(dut->miracast_lib, "dev_exec_action");
	if (!dev_exec_action)
		return -2;
	return dev_exec_action(string_cmd);
}


int miracast_preset_testparameters(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *mdns_disc = get_param(cmd, "mdns_disc");
	const char *mdns_role = get_param(cmd, "mdns_role");
	char string_cmd[MIRACAST_CMD_LEN];
	int ret = 0;
	char string_resp[64] = { 0 };
	int (*extn_sta_preset_test_parameter)(const char *, char *, int);

	miracast_load(dut);
	miracast_generate_string_cmd(cmd, string_cmd, sizeof(string_cmd));
	extn_sta_preset_test_parameter =
		dlsym(dut->miracast_lib, "sta_preset_testparameters");
	if (!extn_sta_preset_test_parameter)
		return -1;
	ret = extn_sta_preset_test_parameter(string_cmd, string_resp,
					     sizeof(string_resp));
	if (ret == SIGMA_ERROR) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "Miracast extension reported error in the command sta_preset_testparameters");
		return 0;
	}

	if (mdns_disc && mdns_role) {
		if (strlen(string_resp))
			strlcpy(dut->mdns_instance_name, string_resp,
				sizeof(dut->mdns_instance_name));
		else
			dut->mdns_instance_name[0] = '\0';
	}

	return 1;
}


static int get_p2p_peers(struct sigma_dut *dut, char *respbuf, size_t bufsize)
{
	char addr[1024], cmd[64];
	const char *intf = get_main_ifname(dut);
	int ret;
	char *pos, *end;

	pos = respbuf;
	end = respbuf + bufsize;

	if (wpa_command_resp(intf, "P2P_PEER FIRST", addr, 128) >= 0) {
		addr[17] = '\0';
		strlcpy(respbuf, addr, bufsize);
		pos += strlen(respbuf);
		ret = snprintf(cmd, sizeof(cmd), "P2P_PEER NEXT-%s", addr);
		if (ret < 0 || ret >= sizeof(cmd))
			return -1;
		memset(addr, 0, sizeof(addr));
		while (wpa_command_resp(intf, cmd, addr, sizeof(addr)) >= 0) {
			if (memcmp(addr, "FAIL", 4) == 0)
				break;
			addr[17] = '\0';
			ret = snprintf(pos, end - pos, " %s", addr);
			if (ret < 0 || ret >= end - pos)
				break;
			pos += ret;
			ret = snprintf(cmd, sizeof(cmd), "P2P_PEER NEXT-%s",
				       addr);
			if (ret < 0 || ret >= sizeof(cmd))
				break;
			memset(addr, 0, sizeof(addr));
		}
	}

	return 0;
}


int miracast_cmd_sta_get_parameter(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	/* const char *program = get_param(cmd, "Program"); */
	const char *parameter = get_param(cmd, "Parameter");
	char resp_buf[1024]; /* may need to change depending on number of peer
				devices found */

	if (!parameter) {
		send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
		return 0;
	}

	if (strcasecmp(parameter, "DiscoveredDevList") == 0) {
		int len = strlen("DeviceList,");

		snprintf(resp_buf, sizeof(resp_buf), "DeviceList,");
		get_p2p_peers(dut, resp_buf + len, 1024 - len);
	} else {
		send_resp(dut, conn, SIGMA_ERROR, "Invalid Parameter");
		return 0;
	}

	send_resp(dut, conn, SIGMA_COMPLETE, resp_buf);
	return 0;
}


int miracast_mdns_start_wfd_connection(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd)
{
	const char *init_wfd = get_param(cmd, "init_wfd");
	int int_init_wfd = -1;
	int rtsp_session_id = -1;
	char cmd_response[128];
	int (*extn_start_wfd_connection)(const char *,
					 const char *, /* Peer IP */
					 int, /* RTSP port number */
					 int, /* WFD Device Type; 0-Source,
						1-P-Sink, 2-Secondary Sink */
					 int *); /* for returning session ID */
	int count = 0;
	char *sig_resp = NULL;

	if (init_wfd)
		int_init_wfd = atoi(init_wfd);

	extn_start_wfd_connection = dlsym(dut->miracast_lib,
					  "start_wfd_connection");
	if (!extn_start_wfd_connection)
		return -1;
	if (int_init_wfd != 0) {
		extn_start_wfd_connection(NULL, NULL, -100,
					  1 - dut->wfd_device_type,
					  &rtsp_session_id);
		while (rtsp_session_id == -1 && count < 60) {
			count++;
			sleep(1);
		}
		snprintf(cmd_response, sizeof(cmd_response),
			 "result,NULL,GroupID,NULL,WFDSessionID,%.8d",
			 rtsp_session_id);
		sig_resp = cmd_response;
	} else {
		rtsp_session_id = 0;
		extn_start_wfd_connection(NULL, NULL, -100,
					  1 - dut->wfd_device_type,
					  &rtsp_session_id);
		sig_resp = "result,NULL,GroupID,NULL,WFDSessionID,NULL";
	}

	send_resp(dut, conn, SIGMA_COMPLETE, sig_resp);
	return 0;
}


static enum sigma_cmd_result cmd_start_wfd_connection(struct sigma_dut *dut,
						      struct sigma_conn *conn,
						      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *peer_address = get_param(cmd, "PeerAddress");
	const char *init_wfd = get_param(cmd, "init_wfd");
	const char *intent_val = get_param(cmd, "intent_val");
	const char *oper_chan = get_param(cmd, "oper_chn");
	const char *coupled_session = get_param(cmd, "coupledSession");
	const char *tdls = get_param(cmd, "TDLS");
	const char *r2_connection = get_param(cmd, "R2ConnectionType");
	char ssid[128];
	char p2p_dev_address[18];
	char output_intf[16];
	char sig_resp_buf[1024];
	char cmd_buf[256]; /* Command buffer */
	char resp_buf[256]; /* Response buffer to UCC */
	int go_intent = 0;
	int freq;
	int res = 0;
	char buf_peer[4096];
	char *availability = NULL;
	char command[64];
	int avail_bit;
	char ctemp[2];
	char rtspport[5] = { '7', '2', '3', '6', '\0' };
	int is_group_owner = 0;
	char peer_ip_address[32];
	int sm_control_port = 7236;
	int rtsp_session_id = -1;
	int (*extn_start_wfd_connection)(const char *,
					 const char *, /* Peer IP */
					 int, /* RTSP port number */
					 int, /* WFD Device Type; 0-Source,
						 1-P-Sink, 2-Secondary Sink */
					 int *); /* for returning session ID */
	int count = 0;

	if (r2_connection) {
		if (strcasecmp(r2_connection, "Infrastructure") == 0)
			return miracast_mdns_start_wfd_connection(dut, conn,
								  cmd);
	}

	if (coupled_session && atoi(coupled_session) == 1) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Coupled Session is unsupported");
		return 0;
	}

	if (tdls && atoi(tdls) == 1) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,TDLS is unsupported");
		return 0;
	}

	if (intent_val) {
		go_intent = atoi(intent_val);
		if (go_intent > 15)
			go_intent = 1;
	}

	if (p2p_discover_peer(dut, intf, peer_address, 1) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Could not find peer");
		return 0;
	}

	snprintf(cmd_buf, sizeof(cmd_buf), "P2P_CONNECT %s", peer_address);

	switch (dut->wps_method) {
	case WFA_CS_WPS_PIN_DISPLAY:
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " %s display",
			 dut->wps_pin);
		break;
	case WFA_CS_WPS_PIN_LABEL:
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " pin label");
		break;
	case WFA_CS_WPS_PIN_KEYPAD:
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " %s keypad",
			 dut->wps_pin);
		break;
	case WFA_CS_WPS_PBC:
	default: /* Configuring default to PBC */
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " pbc");
		break;
	}

	if (dut->persistent) {
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " persistent");
	}
	snprintf(cmd_buf + strlen(cmd_buf), sizeof(cmd_buf) - strlen(cmd_buf),
		 " go_intent=%d", go_intent);

	if (init_wfd && atoi(init_wfd) == 0) {
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " auth");
	}

	if (oper_chan) {
		int chan;

		chan = atoi(oper_chan);
		if (chan >= 1 && chan <= 13)
			freq = 2407 + chan * 5;
		else if (chan == 14)
			freq = 2484;
		else
			freq = 5000 + chan * 5;

		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " freq=%d", freq);
	}

	/* WFD SESSION AVAILABILITY CHECK */

	memset(buf_peer, 0, sizeof(buf_peer));
	snprintf(command, sizeof(command), "P2P_PEER %s", peer_address);
	strlcpy(dut->peer_mac_address, peer_address,
		sizeof(dut->peer_mac_address));
	if (wpa_command_resp(intf, command, buf_peer, sizeof(buf_peer)) >= 0 &&
	    strlen(buf_peer) != 0)
		availability = strstr(buf_peer, "wfd_subelems=");

	if (!availability || strlen(availability) < 21) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Did not get WFD SUBELEMS");
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "result,NULL,GroupID,NULL,WFDSessionID,NULL");
		return 0;
	}

	/* Extracting Availability Bit */
	ctemp[0] = availability[21];
	ctemp[1] = '\0';
	avail_bit = (int) strtol(ctemp, NULL, 16);

	if ((avail_bit & 0x3) == 0) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "result,NULL,GroupID,NULL,WFDSessionID,NULL");
		return 0;
	}

	/* Extract RTSP Port for Sink */

	if (dut->wfd_device_type != 0) {
		if (strlen(availability) >= 23) {
			availability += 23;
			if (availability[0])
				snprintf(rtspport, 5, "%s", availability);
		}
		sigma_dut_print(dut, DUT_MSG_INFO,
				"rtsp_port = %s, availability = %s ",
				rtspport, availability);
		session_management_control_port = (int) strtol(rtspport, NULL,
							       16);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"SessionManagementControlPort = %d",
				session_management_control_port);
	}

	memset(resp_buf, 0, sizeof(resp_buf));
	res = wpa_command_resp(intf, cmd_buf, resp_buf, sizeof(resp_buf));
	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"wpa_command_resp failed");
		return 1;
	}
	if (strncmp(resp_buf, "FAIL", 4) == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"wpa_command: Command failed (FAIL received)");
		return 1;
	}

	if (init_wfd && atoi(init_wfd) == 0) {
		/* Start thread to wait for P2P connection */
		miracast_rtsp_thread_create(dut, conn, cmd);
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "result,NULL,GroupID,NULL,WFDSessionID,NULL");
		return 0;
	}

	res = get_p2p_connection_event(dut, intf, output_intf,
				       sizeof(output_intf), &is_group_owner);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "p2p connection done %d",
			is_group_owner);
	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Group Formation did not complete");
		return 1;
	}

	snprintf(sig_resp_buf, sizeof(sig_resp_buf), "result");

	if (is_group_owner) {
		stop_dhcp(dut, output_intf,1);
		snprintf(sig_resp_buf + strlen(sig_resp_buf),
			 sizeof(sig_resp_buf) - strlen(sig_resp_buf), ",GO");
		start_dhcp(dut, output_intf,1);
		sleep(5);
	} else {
		snprintf(sig_resp_buf + strlen(sig_resp_buf),
			 sizeof(sig_resp_buf) - strlen(sig_resp_buf),
			 ",CLIENT");
		miracast_start_dhcp_client(dut, output_intf);
		sleep(5);
	}

	snprintf(sig_resp_buf + strlen(sig_resp_buf),
		 sizeof(sig_resp_buf) - strlen(sig_resp_buf), ",GroupID,");

	res = get_wpa_status(output_intf, "p2p_device_address",
			     p2p_dev_address, sizeof(p2p_dev_address));
	if (res < 0)
		return -1;
	sigma_dut_print(dut, DUT_MSG_INFO, "p2p_dev_address %s",
			p2p_dev_address);
	strlcpy(sig_resp_buf + strlen(sig_resp_buf), p2p_dev_address,
		sizeof(sig_resp_buf) - strlen(sig_resp_buf));

	res = get_wpa_status(output_intf, "ssid", ssid, sizeof(ssid));
	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"get_wpa_status failed to get ssid");
		return -1;
	}

	snprintf(sig_resp_buf + strlen(sig_resp_buf),
		 sizeof(sig_resp_buf) - strlen(sig_resp_buf),
		 " %s,WFDSessionId,", ssid);

	if (!is_group_owner) {
		if (get_peer_ip_p2p_client(dut, peer_ip_address, output_intf,
					   60) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "Could not get remote IP");
			return 0;
		}
	} else {
		if (get_peer_ip_p2p_go(dut, peer_ip_address, 30) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "Could not get remote IP");
			return 0;
		}
	}

	if (dut->wfd_device_type != 0)
		sm_control_port = (int) strtol(rtspport, NULL, 16);
	else
		sm_control_port = 7236;

	extn_start_wfd_connection = dlsym(dut->miracast_lib,
					  "start_wfd_connection");
	if (!extn_start_wfd_connection)
		return -1;
	extn_start_wfd_connection(NULL, peer_ip_address, sm_control_port,
				  1 - dut->wfd_device_type, &rtsp_session_id);

	while (rtsp_session_id == -1 && count < 60) {
		count++;
		sleep(1);
	}

	snprintf(sig_resp_buf + strlen(sig_resp_buf),
		 sizeof(sig_resp_buf) - strlen(sig_resp_buf), "%.8d",
		 rtsp_session_id);
	send_resp(dut, conn, SIGMA_COMPLETE, sig_resp_buf);
	return 0;
}


static enum sigma_cmd_result cmd_connect_go_start_wfd(struct sigma_dut *dut,
						      struct sigma_conn *conn,
						      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *p2p_dev_id = get_param(cmd, "P2PdevID");
	/* const char *p2p_group_id = get_param(cmd, "GroupID"); */
	char sig_resp_buf[1024];
	char method[12];
	char cmd_buf[256];
	char buf[256];
	char resp_buf[256];
	int go = 0;
	int res = 0;
	char output_ifname[32];
	char peer_ip_address[32];
	int rtsp_session_id = -1;
	int (*extn_connect_go_start_wfd)(const char *,
					 const char * /* Peer IP */,
					 int /* RTSP port number */,
					 int /* WFD Device Type; 0-Source,
						1-P-Sink, 2-Secondary Sink */,
					 int *); /* for returning session ID */

	snprintf(cmd_buf, sizeof(cmd_buf), "P2P_CONNECT %s", p2p_dev_id);

	switch (dut->wps_method) {
	case WFA_CS_WPS_PBC:
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " pbc");
		strlcpy(method, "pbc", sizeof(method));
		break;
	case WFA_CS_WPS_PIN_DISPLAY:
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " %s display",
			 dut->wps_pin);
		strlcpy(method, "display", sizeof(method));
		break;
	case WFA_CS_WPS_PIN_LABEL:
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " pin label");
		strlcpy(method, "label", sizeof(method));
		break;
	case WFA_CS_WPS_PIN_KEYPAD:
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " %s keypad",
			 dut->wps_pin);
		strlcpy(method, "keypad", sizeof(method));
		break;
	default: /* Configuring to PBC */
		snprintf(cmd_buf + strlen(cmd_buf),
			 sizeof(cmd_buf) - strlen(cmd_buf), " pbc");
		strlcpy(method, "pbc", sizeof(method));
		break;
	}
	snprintf(cmd_buf + strlen(cmd_buf),
		 sizeof(cmd_buf) - strlen(cmd_buf), " join");

	/* run provisional discovery */
	if (p2p_discover_peer(dut, intf, p2p_dev_id, 0) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Could not discover the requested peer");
		return 0;
	}

	snprintf(buf, sizeof(buf), "P2P_PROV_DISC %s %s", p2p_dev_id, method);
	if (wpa_command(intf, buf) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to send provision discovery request");
		return -2;
	}

	res = wpa_command_resp(intf, cmd_buf, resp_buf, sizeof(resp_buf));
	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"wpa_command_resp failed");
		return 1;
	}
	if (strncmp(resp_buf, "FAIL", 4) == 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,failed P2P connection");
		return 0;
	}

	res = get_p2p_connection_event(dut, intf, output_ifname,
				       sizeof(output_ifname), &go);
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,failed P2P connection");
		return 0;
	}

	miracast_start_dhcp_client(dut, output_ifname);

	if (get_peer_ip_p2p_client(dut, peer_ip_address, output_ifname,
				   30) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "Could not get remote IP");
		return 0;
	}

	if (dut->wfd_device_type != 0) {
		char rtsp_buff[1000], cmd_buff[32];
		char *sub_elem = NULL;
		char rtspport[5] = { '7', '2', '3', '6', '\0' };

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Log --- p2p address = %s", p2p_dev_id);
		snprintf(cmd_buff, sizeof(cmd_buff), "P2P_PEER %s", p2p_dev_id);
		if (wpa_command_resp(output_ifname, cmd_buff, rtsp_buff,
				     sizeof(rtsp_buff)) >= 0 &&
		    strlen(rtsp_buff) != 0)
			sub_elem = strstr(rtsp_buff, "wfd_subelems=");

		/* Extract RTSP Port for Sink */
		if (sub_elem && strlen(sub_elem) >= 23) {
			sub_elem += 23;
			snprintf(rtspport, 5, "%s", sub_elem);
			sigma_dut_print(dut, DUT_MSG_DEBUG,
				"rtsp_port = %s, subElem = %s",
				rtspport, sub_elem);
		}
		session_management_control_port = (int) strtol(rtspport, NULL,
							       16);
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"SessionManagementControlPort = %d",
				session_management_control_port);

	} else {
		session_management_control_port = 7236;
	}

	extn_connect_go_start_wfd = dlsym(dut->miracast_lib,
					  "connect_go_start_wfd");
	if (!extn_connect_go_start_wfd)
		return -1;
	extn_connect_go_start_wfd(NULL, peer_ip_address,
				  session_management_control_port,
				  1 - dut->wfd_device_type, &rtsp_session_id);
	/* Null terminating regardless of what was returned */
	snprintf(sig_resp_buf, sizeof(sig_resp_buf), "WFDSessionId,%.8d",
		 rtsp_session_id);

	send_resp(dut, conn, SIGMA_COMPLETE, sig_resp_buf);
	return 0;
}


static enum sigma_cmd_result cmd_sta_generate_event(struct sigma_dut *dut,
						    struct sigma_conn *conn,
						    struct sigma_cmd *cmd)
{

	/* const char *intf = get_param(cmd, "Interface"); */
	/* const char *program = get_param(cmd, "Program"); */
	const char *type = get_param(cmd, "Type");
	char string_cmd[MIRACAST_CMD_LEN];
	int (*extn_sta_generate_event)(const char *);

	if (!type) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode, Invalid Type for Generate Event");
		return 0;
	}
	miracast_generate_string_cmd(cmd, string_cmd, sizeof(string_cmd));
	extn_sta_generate_event = dlsym(dut->miracast_lib,
					"sta_generate_event");
	if (!extn_sta_generate_event)
		return -1;
	if (strcasecmp(type, "UIBC_Gen") == 0 ||
	    strcasecmp(type, "UIBC_HID") == 0) {
		extn_sta_generate_event(string_cmd);
	} else if (strcasecmp(type, "FrameSkip") == 0) {
		return 1;
	} else if (strcasecmp(type, "InputContent") == 0) {
		send_resp(dut, conn, SIGMA_COMPLETE, NULL);
		return 0;
	} else if (strcasecmp(type, "I2cRead") == 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported Type for Generate Event");
		return 0;
	} else if (strcasecmp(type, "I2cWrite") == 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode, Unsupported Type for Generate Event");
		return 0;
	} else if (strcasecmp(type, "IdrReq") == 0) {
		if (dut->wfd_device_type == 0) { /* Source */
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode, Unsupported Type for Generate Event");
		} else {
			send_resp(dut, conn, SIGMA_COMPLETE, NULL);
			extn_sta_generate_event(string_cmd);
		}
		return 0;
	}
	return 1;
}


static enum sigma_cmd_result cmd_reinvoke_wfd_session(struct sigma_dut *dut,
						      struct sigma_conn *conn,
						      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *grp_id = get_param(cmd, "GroupID");
	const char *peer_address = get_param(cmd, "peeraddress");
	const char *invitation_action = get_param(cmd, "InvitationAction");
	char buf[256];
	struct wpa_ctrl *ctrl;
	int res, id;
	char *ssid, *pos;
	unsigned int wait_limit;
	char peer_ip_address[32];
	int rtsp_session_id = -1;
	int (*extn_start_wfd_connection)(const char *,
					 const char *, /* Peer IP */
					 int, /* RTSP port number */
					 int, /* WFD Device Type; 0-Source,
						 1-P-Sink, 2-Secondary Sink */
					 int *); /* for returning session ID */

	/* All are compulsory parameters */
	if (!intf || !grp_id || !invitation_action || !peer_address) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode,Invalid parameters for Reinvoke WFD Session");
		return 0;
	}

	if (strcmp(invitation_action, "accept") == 0) {
		/*
		 * In a client-joining-a-running-group case, we need to
		 * separately authorize the invitation.
		 */
		miracast_stop_dhcp_client(dut, NULL);
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Trying to discover GO %s",
				peer_address);
		if (p2p_discover_peer(dut, intf, peer_address, 1) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Could not discover the requested peer");
			return 0;
		}

		snprintf(buf, sizeof(buf), "P2P_CONNECT %s %s join auth",
			 peer_address, dut->wps_method == WFA_CS_WPS_PBC ?
			 "pbc" : dut->wps_pin);
		if (wpa_command(intf, buf) < 0)
			return -2;

		miracast_rtsp_thread_create(dut, conn, cmd);
		return 1;
	}

	ssid = strchr(grp_id, ' ');
	if (!ssid) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Invalid grpid");
		return -1;
	}
	ssid++;
	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Search for persistent group credentials based on SSID: '%s'",
			ssid);
	if (wpa_command_resp(intf, "LIST_NETWORKS", buf, sizeof(buf)) < 0)
		return -2;
	pos = strstr(buf, ssid);
	if (!pos || pos == buf || pos[-1] != '\t' ||
	    pos[strlen(ssid)] != '\t') {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Persistent group credentials not found");
		return 0;
	}
	while (pos > buf && pos[-1] != '\n')
		pos--;
	id = atoi(pos);
	snprintf(buf, sizeof(buf), "P2P_INVITE persistent=%d peer=%s",
		 id, peer_address);

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Trying to discover peer %s for invitation",
			peer_address);
	if (p2p_discover_peer(dut, intf, peer_address, 0) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Could not discover the requested peer");
		return 0;
	}

	ctrl = open_wpa_mon(intf);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return -2;
	}

	if (wpa_command(intf, buf) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to send invitation request");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return -2;
	}

	res = get_wpa_cli_event(dut, ctrl, "P2P-INVITATION-RESULT",
				buf, sizeof(buf));
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);
	if (res < 0)
		return -2;

	miracast_load(dut);

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Waiting to start DHCP");

	/* Calling Miracast multimedia APIs */
	if (dut->wfd_device_type != 0)
		wait_limit = HUNDRED_SECOND_TIMEOUT;
	else
		wait_limit = 500;

	stop_dhcp(dut, intf, 1);
	/* For GO read the DHCP lease file */
	sigma_dut_print(dut, DUT_MSG_INFO, "Waiting to start DHCP server");
	start_dhcp(dut, intf, 1);
	sleep(5);
	if (get_peer_ip_p2p_go(dut, peer_ip_address, wait_limit) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Could not get peer IP");
		return -2;
	}

	extn_start_wfd_connection = dlsym(dut->miracast_lib,
					  "start_wfd_connection");
	if (extn_start_wfd_connection) {
		extn_start_wfd_connection(NULL, peer_ip_address,
					  session_management_control_port,
					  1 - dut->wfd_device_type,
					  &rtsp_session_id);
	} else {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"dlsym seems to have error %p %p",
				dut->miracast_lib,
				extn_start_wfd_connection);
	}

	return 1;
}


static int req_intf_peer(struct sigma_cmd *cmd)
{
	if (!get_param(cmd, "interface") ||
	    !get_param(cmd, "PeerAddress"))
		return -1;
	return 0;
}


static int req_intf_p2pdev_grpid(struct sigma_cmd *cmd)
{
	if (!get_param(cmd, "interface") ||
	    !get_param(cmd, "P2pdevID") ||
	    !get_param(cmd, "GroupID"))
		return -1;
	return 0;
}


static int req_intf_prog_type(struct sigma_cmd *cmd)
{
	const char *prog = get_param(cmd, "Program");

	if (!get_param(cmd, "interface") ||
	    !get_param(cmd, "Type") ||
	    !prog || strcmp(prog, "WFD") != 0)
		return -1;
	return 0;
}


static int req_intf_peeradd_inv(struct sigma_cmd *cmd)
{
	if (!get_param(cmd, "interface") ||
	    !get_param(cmd, "peerAddress") ||
	    !get_param(cmd, "InvitationAction"))
		return -1;
	return 0;
}


void miracast_register_cmds(void)
{
	sigma_dut_reg_cmd("start_wfd_connection", req_intf_peer,
			  cmd_start_wfd_connection);
	sigma_dut_reg_cmd("connect_go_start_wfd", req_intf_p2pdev_grpid,
			  cmd_connect_go_start_wfd);
	sigma_dut_reg_cmd("sta_generate_event", req_intf_prog_type,
			  cmd_sta_generate_event);
	sigma_dut_reg_cmd("reinvoke_wfd_session", req_intf_peeradd_inv,
			  cmd_reinvoke_wfd_session);
}
