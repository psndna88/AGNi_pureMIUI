/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 * Copyright (c) 2011-2014, 2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2019, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"

void sigma_dut_register_cmds(void)
{
	basic_register_cmds();
	sta_register_cmds();
	traffic_register_cmds();
#ifdef CONFIG_TRAFFIC_AGENT
	traffic_agent_register_cmds();
#endif /* CONFIG_TRAFFIC_AGENT */
	p2p_register_cmds();
	ap_register_cmds();
	powerswitch_register_cmds();
	atheros_register_cmds();
#ifdef CONFIG_WLANTEST
	wlantest_register_cmds();
#endif /* CONFIG_WLANTEST */
	dev_register_cmds();
#ifdef CONFIG_SNIFFER
	sniffer_register_cmds();
#endif /* CONFIG_SNIFFER */
#ifdef CONFIG_SERVER
	server_register_cmds();
#endif /* CONFIG_SERVER */
#ifdef MIRACAST
	miracast_register_cmds();
#endif /* MIRACAST */
}
