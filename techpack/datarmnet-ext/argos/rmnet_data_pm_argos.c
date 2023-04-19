/*
 * rmnet_data_pm_argos.c
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

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpumask.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/pm_qos.h>
#include <linux/suspend.h>
#include "rmnet_data_pm_argos.h"

/*
 * argos rmnet speed matching table
 *
 * pm qos threshold : 10 Mbps
 * tx aggre threshold : 100 Mbps
 * ipa napi chained rx threshold : 100 Mbps
 * big core boot threshold : 300 Mbps
 *
 *  ------------------------------- default ---------------------------
 *  speed	rps	gro_cnt	napi_chain	pm_qos	tx_aggr
 *  -------------------------------------------------------------------
 *  0		7d	2	disable		disable	disable
 *  10		7d	2	disable		enable	disable
 *  30		7d	2	disable		enable	disable
 *  60		7d	2	disable		enable	disable
 *  100		7d	5	enable		enable	enable
 *  200		7d	5	enable		enable	enable
 *  300		70	0(max)	enable		enable	enable
 *
 */

#define MIF_ARGOS_IPC_LABEL "IPC"
#define RMNET_DATA_MAX_VND	8
const char *ndev_prefix = "rmnet_data";
static struct rmnet_data_pm_config *cfg;

/* rps boosting */
#define ARGOS_RMNET_RPS_BIG_MASK "70" /* big core*/
#define ARGOS_RMNET_RPS_DEFAULT_MASK "7d" /* default */
#define ARGOS_RMNET_RPS_BOOST_MBPS 300
static unsigned int rmnet_rps_boost_mbps = ARGOS_RMNET_RPS_BOOST_MBPS;
module_param(rmnet_rps_boost_mbps, uint, 0644);
MODULE_PARM_DESC(rmnet_rps_boost_mbps, "Rps Boost Threshold");
static bool rmnet_data_pm_in_boost;

/* pm qos */
static struct pm_qos_request rmnet_qos_req;
#define ARGOS_RMNET_PM_QOS_MBPS 10
static unsigned int rmnet_pm_qos_mbps = ARGOS_RMNET_PM_QOS_MBPS;
module_param(rmnet_pm_qos_mbps, uint, 0644);
MODULE_PARM_DESC(rmnet_rps_boost_mbps, "PM QOS Threshold");
#define PM_QOS_RMNET_LATENCY_VALUE 44
static bool pm_qos_requested;

/* tx aggregation */
#define ARGOS_RMNET_TX_AGGR_MBPS 100
static unsigned int rmnet_tx_aggr_mbps = ARGOS_RMNET_TX_AGGR_MBPS;
module_param(rmnet_tx_aggr_mbps, uint, 0644);
MODULE_PARM_DESC(rmnet_tx_aggr_mbps, "TX aggr Threshold");
bool rmnet_data_tx_aggr_enabled;

/* ipa napi chained rx */
#define ARGOS_RMNET_IPA_NAPI_CHAIN_MBPS 100
static unsigned int rmnet_ipa_napi_chain_mbps = ARGOS_RMNET_IPA_NAPI_CHAIN_MBPS;
module_param(rmnet_ipa_napi_chain_mbps, uint, 0644);
MODULE_PARM_DESC(rmnet_ipa_napi_chain_mbps, "IPA NAPI chained rx Threshold");
//extern void ipa3_set_napi_chained_rx(bool enable);

/* mhi napi chained rx */
#define ARGOS_RMNET_MHI_NAPI_CHAIN_MBPS 200
static unsigned int rmnet_mhi_napi_chain_mbps = ARGOS_RMNET_MHI_NAPI_CHAIN_MBPS;

/* gro count variation */
u32 config_flushcount = 2;
#define RMNET_GRO_CNT_LVL1_MBPS 100
#define RMNET_GRO_CNT_LVL2_MBPS 300
#define RMNET_GRO_LVL1_CNT 2
#define RMNET_GRO_LVL2_CNT 5
#define RMNET_GRO_MAX_CNT 0

#define get_gro_cnt(speed) (speed < RMNET_GRO_CNT_LVL1_MBPS ?	\
			    RMNET_GRO_LVL1_CNT :		\
			    (speed < RMNET_GRO_CNT_LVL2_MBPS ?	\
			     RMNET_GRO_LVL2_CNT : RMNET_GRO_MAX_CNT))

#ifdef CONFIG_RPS
/* reference :  net/core/net-sysfs.c store_rps_map() */
static int rmnet_data_pm_change_rps_map(struct netdev_rx_queue *queue,
		      const char *buf, size_t len)
{
	struct rps_map *old_map, *map;
	cpumask_var_t mask;
	int err, cpu, i;
	static DEFINE_MUTEX(rps_map_mutex);

	pr_debug("rmnet_data_pm_change_rps_map %s %d\n", buf, len);

	if (!alloc_cpumask_var(&mask, GFP_KERNEL)){
		pr_err("%s alloc_cpumask_var fail\n", __func__);
		return -ENOMEM;
	}
	err = bitmap_parse(buf, len, cpumask_bits(mask), nr_cpumask_bits);
	if (err) {
		free_cpumask_var(mask);
		pr_err("%s bitmap_parse err %d\n", __func__, err);
		return err;
	}

	map = kzalloc(max_t(unsigned long,
	    RPS_MAP_SIZE(cpumask_weight(mask)), L1_CACHE_BYTES),
	    GFP_KERNEL);
	if (!map) {
		free_cpumask_var(mask);
		pr_err("rmnet_data_pm_change_rps_map kzalloc fail\n");
		return -ENOMEM;
	}

	i = 0;
	for_each_cpu_and(cpu, mask, cpu_online_mask) {
		map->cpus[i++] = cpu;
	}

	if (i) {
		map->len = i;
	} else {
		kfree(map);
		map = NULL;

		free_cpumask_var(mask);
		pr_err("failed to map rps_cpu\n");
		return -EINVAL;
	}

	mutex_lock(&rps_map_mutex);
	old_map = rcu_dereference_protected(queue->rps_map,
					    mutex_is_locked(&rps_map_mutex));
	rcu_assign_pointer(queue->rps_map, map);

	if (map)
		static_key_slow_inc((struct static_key *)&rps_needed);
	if (old_map)
		static_key_slow_dec((struct static_key *)&rps_needed);

	mutex_unlock(&rps_map_mutex);

	if (old_map)
		kfree_rcu(old_map, rcu);

	free_cpumask_var(mask);
	return map->len;
}
#else
#define rmnet_data_pm_change_rps_map(queue, buf, len) do { } while (0)
#endif /* CONFIG_RPS */

static int rmnet_data_pm_set_rps(char *buf, int len)
{
	char ndev_name[IFNAMSIZ];
	struct net_device *ndev;
	int i, ret = 0;

	/* get rx_queue from net devices pointer */
	for (i = 0; i < RMNET_DATA_MAX_VND; i++) {
		memset(ndev_name, 0, IFNAMSIZ);
		snprintf(ndev_name, IFNAMSIZ, "%s%d", ndev_prefix, i);

		ndev = dev_get_by_name(&init_net, ndev_name);
		if (!ndev) {
			pr_info("Cannot find %s from init_net\n", ndev_name);
			continue;
		}

		ret = rmnet_data_pm_change_rps_map(ndev->_rx, buf, len);
		if (ret < 0)
			pr_err("set rps %s:%s, err %d\n", ndev->name, buf, ret);

		pr_err("set rps %s:%s, done\n", ndev->name, buf);

		dev_put(ndev);
	}

	return ret;
}

static void rmnet_data_pm_set_pm_qos(unsigned long speed)
{
	if (speed >= rmnet_pm_qos_mbps && !pm_qos_requested) {
		pr_info("%s pm_qos_add_request\n", __func__);
		pm_qos_add_request(&rmnet_qos_req, PM_QOS_CPU_DMA_LATENCY,
				   PM_QOS_RMNET_LATENCY_VALUE);
		pm_qos_requested = true;
	} else if (speed < rmnet_pm_qos_mbps && pm_qos_requested) {
		pr_info("%s pm_qos_remove_request\n", __func__);
		pm_qos_remove_request(&rmnet_qos_req);
		pm_qos_requested = false;
	}
}

static void rmnet_data_pm_set_tx_aggr(unsigned long speed)
{
	if (speed >= rmnet_tx_aggr_mbps) {
		pr_info("%s enabled\n", __func__);
		rmnet_data_tx_aggr_enabled = true;
	} else {
		pr_info("%s disabled\n", __func__);
		rmnet_data_tx_aggr_enabled = false;
	}
}

static void rmnet_data_pm_set_gro_cnt(unsigned long speed)
{
	config_flushcount = get_gro_cnt(speed);
}

static void rmnet_data_pm_boost_rps(unsigned long speed)
{
	if (speed >= rmnet_rps_boost_mbps && !rmnet_data_pm_in_boost) {
		pr_info("Speed: %luMbps, %s -> %s\n", speed,
		ARGOS_RMNET_RPS_DEFAULT_MASK, ARGOS_RMNET_RPS_BIG_MASK);

		rmnet_data_pm_set_rps(ARGOS_RMNET_RPS_BIG_MASK,
				      strlen(ARGOS_RMNET_RPS_BIG_MASK));


		rmnet_data_pm_in_boost = true;
	} else if (speed < rmnet_rps_boost_mbps && rmnet_data_pm_in_boost) {
		pr_info("Speed: %luMbps, %s -> %s\n", speed,
		ARGOS_RMNET_RPS_BIG_MASK, ARGOS_RMNET_RPS_DEFAULT_MASK);
		rmnet_data_pm_set_rps(ARGOS_RMNET_RPS_DEFAULT_MASK,
				      strlen(ARGOS_RMNET_RPS_DEFAULT_MASK));

		rmnet_data_pm_in_boost = false;

	}
}

static void rmnet_data_pm_set_mhi_napi_chain(unsigned long speed)
{
	if (speed >= rmnet_mhi_napi_chain_mbps) {
		mhi_set_napi_chained_rx(cfg->real_dev, true);
	} else {
		mhi_set_napi_chained_rx(cfg->real_dev, false);
	}
}

static void rmnet_data_pm_set_ipa_napi_chain(unsigned long speed)
{
	if (speed >= rmnet_ipa_napi_chain_mbps) {
		pr_info("%s enabled\n", __func__);
		//ipa3_set_napi_chained_rx(true);
	} else {
		pr_info("%s disabled\n", __func__);
		//ipa3_set_napi_chained_rx(false);
	}
}

struct rmnet_data_pm_ops rmnet_mhi_ops = {
	.boost_rps = rmnet_data_pm_boost_rps,
	.pnd_chain = rmnet_data_pm_set_mhi_napi_chain,
	.gro_count = rmnet_data_pm_set_gro_cnt,
	.tx_aggr = rmnet_data_pm_set_tx_aggr,
	.pm_qos = rmnet_data_pm_set_pm_qos,
};

struct rmnet_data_pm_ops rmnet_ipa_ops = {
	.boost_rps = rmnet_data_pm_boost_rps,
	.pnd_chain = rmnet_data_pm_set_ipa_napi_chain,
	.gro_count = rmnet_data_pm_set_gro_cnt,
	.tx_aggr = rmnet_data_pm_set_tx_aggr,
	.pm_qos = rmnet_data_pm_set_pm_qos,
};

struct rmnet_data_pm_ops rmnet_dummy_ops = {
	.boost_rps = NULL,
	.pnd_chain = NULL,
	.gro_count = NULL,
	.tx_aggr = NULL,
	.pm_qos = NULL,
};

void rmnet_data_pm_set_ops(struct net_device *real_dev)
{
	if (!strncmp(real_dev->name, "rmnet_mhi", strlen("rmnet_mhi")))
		cfg->ops = &rmnet_mhi_ops;
	else if (!strncmp(real_dev->name, "rmnet_ipa", strlen("rmnet_ipa")))
		cfg->ops = &rmnet_ipa_ops;
	else
		cfg->ops = &rmnet_dummy_ops;
	pr_info("%s as %pf for %s\n", __func__, cfg->ops, real_dev->name);
}

/* argos event callback : speed notified deaclared in argos table */
static int rmnet_data_pm_argos_cb(struct notifier_block *nb,
				  unsigned long speed, void *data)
{
	pr_info("%s in speed %lu Mbps\n", __func__, speed);

	if (cfg->ops->pm_qos)
		cfg->ops->pm_qos(speed);

	/*
	if (cfg->ops->boost_rps)
		cfg->ops->boost_rps(speed);

	if (cfg->ops->pnd_chain)
		cfg->ops->pnd_chain(speed);
	if (cfg->ops->gro_count)
		cfg->ops->gro_count(speed);
	if (cfg->ops->tx_aggr)
		cfg->ops->tx_aggr(speed);
	*/

	return NOTIFY_DONE;
}

static struct notifier_block rmnet_data_argos_nb __read_mostly = {
	.notifier_call = rmnet_data_pm_argos_cb,
};

static int rmnet_data_dev_cb(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct net_device *dev = netdev_notifier_info_to_dev(data);
	struct rmnet_priv *priv;
	int ret;

	if (strncmp(dev->name, ndev_prefix, strlen(ndev_prefix)) ||
			dev->type != ARPHRD_RAWIP)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
		if (cfg)
			break;

		priv = netdev_priv(dev);
		if (priv && priv->real_dev)
			pr_info("real_dev of %s is %s\n", dev->name, priv->real_dev->name);
		else
			break;

		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			pr_err("Fail to allocate rmnet_data_pm config\n");
			break;
		}

		cfg->real_dev = priv->real_dev;
		rmnet_data_pm_set_ops(cfg->real_dev);

		/* Set initial value */
		rmnet_data_pm_argos_cb(NULL, 0, NULL);

		ret = register_pm_notifier(&rmnet_data_argos_nb);
		if (ret)
			pr_err("Fail to register rmnet_data pm argos notifier block\n");
		break;
	case NETDEV_UNREGISTER:
		if (!cfg)
			break;

		pr_info("Reset rmnet_data_pm_argos configure\n");
		ret = unregister_pm_notifier(&rmnet_data_argos_nb);
		if (ret)
			pr_err("Fail to unregister rmnet_data pm argos notifier block\n");

		kfree(cfg);
		cfg = NULL;
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block rmnet_data_dev_nb __read_mostly = {
	.notifier_call = rmnet_data_dev_cb,
};

static int __init rmnet_data_pm_argos_init(void)
{
	int ret = register_netdevice_notifier(&rmnet_data_dev_nb);

	if (ret) {
		pr_err("Fail to register rmnet_data device notifier block\n");
	}
	return ret;
}

static void __exit rmnet_data_pm_argos_exit(void)
{
	unregister_netdevice_notifier(&rmnet_data_dev_nb);
}

module_init(rmnet_data_pm_argos_init);
module_exit(rmnet_data_pm_argos_exit);
MODULE_LICENSE("GPL");
