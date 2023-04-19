/*
 * rmnet_data_pm_argos.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */


#ifndef _RMNET_DATA_PM_ARGOS_H_
#define _RMNET_DATA_PM_ARGOS_H_

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/rmnet_config.h>
#include <net/gro_cells.h>

struct rmnet_data_pm_ops {
    void (*boost_rps) (unsigned long speed);
    void (*pnd_chain) (unsigned long speed);
    void (*gro_count) (unsigned long speed);
    void (*tx_aggr) (unsigned long speed);
    void (*pm_qos) (unsigned long speed);
};

struct rmnet_data_pm_config {
    struct net_device *real_dev;
    struct rmnet_data_pm_ops *ops;
};


struct rmnet_vnd_stats {
	u64 rx_pkts;
	u64 rx_bytes;
	u64 tx_pkts;
	u64 tx_bytes;
	u32 tx_drops;
};

struct rmnet_pcpu_stats {
	struct rmnet_vnd_stats stats;
	struct u64_stats_sync syncp;
};

struct rmnet_coal_close_stats {
	u64 non_coal;
	u64 ip_miss;
	u64 trans_miss;
	u64 hw_nl;
	u64 hw_pkt;
	u64 hw_byte;
	u64 hw_time;
	u64 hw_evict;
	u64 coal;
};

struct rmnet_coal_stats {
	u64 coal_rx;
	u64 coal_pkts;
	u64 coal_hdr_nlo_err;
	u64 coal_hdr_pkt_err;
	u64 coal_csum_err;
	u64 coal_reconstruct;
	u64 coal_ip_invalid;
	u64 coal_trans_invalid;
	struct rmnet_coal_close_stats close;
	u64 coal_veid[4];
	u64 coal_tcp;
	u64 coal_tcp_bytes;
	u64 coal_udp;
	u64 coal_udp_bytes;
};

struct rmnet_priv_stats {
	u64 csum_ok;
	u64 csum_valid_unset;
	u64 csum_validation_failed;
	u64 csum_err_bad_buffer;
	u64 csum_err_invalid_ip_version;
	u64 csum_err_invalid_transport;
	u64 csum_fragmented_pkt;
	u64 csum_skipped;
	u64 csum_sw;
	u64 csum_hw;
	struct rmnet_coal_stats coal;
	u64 ul_prio;
};

struct rmnet_priv {
	u8 mux_id;
	struct net_device *real_dev;
	struct rmnet_pcpu_stats __percpu *pcpu_stats;
	struct gro_cells gro_cells;
	struct rmnet_priv_stats stats;
	void __rcu *qos_info;
};


#if defined (CONFIG_IPA3)
extern void ipa3_set_napi_chained_rx(bool enable);
#else
static void ipa3_set_napi_chained_rx(bool enable)
{
    return 0;
}
#endif

#if defined (CONFIG_MHI_NETDEV)
extern void mhi_set_napi_chained_rx(struct net_device *dev, bool enable);
#else
static void mhi_set_napi_chained_rx(struct net_device *dev, bool enable)
{
    return;
}
#endif

#endif /* _RMNET_DATA_PM_ARGOS_H_ */
