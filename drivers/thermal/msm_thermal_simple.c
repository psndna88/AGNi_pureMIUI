// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#define pr_fmt(fmt) "msm_thermal_simple: " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/qpnp/qpnp-adc.h>

#define OF_READ_U32(node, prop, dst)						\
({										\
	int ret = of_property_read_u32(node, prop, &(dst));			\
	if (ret)								\
		pr_err("%s: " prop " property missing\n", (node)->name);	\
	ret;									\
})

struct thermal_zone {
	u32 gold_khz;
	u32 silver_khz;
	s32 trip_deg;
};

struct thermal_drv {
	struct notifier_block cpu_notif;
	struct delayed_work throttle_work;
	struct workqueue_struct *wq;
	struct thermal_zone *zones;
	struct qpnp_vadc_chip *vadc_dev;
	struct thermal_zone *curr_zone;
	enum qpnp_vadc_channels adc_chan;
	u32 poll_jiffies;
	u32 start_delay;
	u32 nr_zones;
};

static void update_online_cpu_policy(void)
{
	u32 cpu;

	/* Only one CPU from each cluster needs to be updated */
	get_online_cpus();
	cpu = cpumask_first_and(cpu_lp_mask, cpu_online_mask);
	cpufreq_update_policy(cpu);
	cpu = cpumask_first_and(cpu_perf_mask, cpu_online_mask);
	cpufreq_update_policy(cpu);
	put_online_cpus();
}

static void thermal_throttle_worker(struct work_struct *work)
{
	struct thermal_drv *t = container_of(to_delayed_work(work), typeof(*t),
					     throttle_work);
	struct thermal_zone *new_zone, *old_zone;
	struct qpnp_vadc_result result;
	s64 temp_deg;
	int i, ret;

	ret = qpnp_vadc_read(t->vadc_dev, t->adc_chan, &result);
	if (ret) {
		pr_err("Unable to read ADC channel, err: %d\n", ret);
		goto reschedule;
	}

	temp_deg = result.physical;
#ifdef TEMP_DEBUG
	printk("thermal reading is: %lld\n", temp_deg);
#endif
	old_zone = t->curr_zone;
	new_zone = NULL;

	for (i = t->nr_zones - 1; i >= 0; i--) {
		if (temp_deg >= t->zones[i].trip_deg) {
			new_zone = t->zones + i;
			break;
		}
	}

	/* Update thermal zone if it changed */
	if (new_zone != old_zone) {
		t->curr_zone = new_zone;
		update_online_cpu_policy();
	}

reschedule:
	queue_delayed_work(t->wq, &t->throttle_work, t->poll_jiffies);
}

static u32 get_throttle_freq(struct thermal_zone *zone, u32 cpu)
{
	if (cpumask_test_cpu(cpu, cpu_lp_mask))
		return zone->silver_khz;

	return zone->gold_khz;
}

static int cpu_notifier_cb(struct notifier_block *nb, unsigned long val,
			   void *data)
{
	struct thermal_drv *t = container_of(nb, typeof(*t), cpu_notif);
	struct cpufreq_policy *policy = data;
	struct thermal_zone *zone;

	if (val != CPUFREQ_ADJUST)
		return NOTIFY_OK;

	zone = t->curr_zone;

	if (zone) {
		u32 target_freq = get_throttle_freq(zone, policy->cpu);

		if (target_freq < policy->max)
			policy->max = target_freq;
	} else {
		policy->max = policy->user_policy.max;
	}

	if (policy->max < policy->min)
		policy->min = policy->max;

	return NOTIFY_OK;
}

static int msm_thermal_simple_parse_dt(struct platform_device *pdev,
				       struct thermal_drv *t)
{
	struct device_node *child, *node = pdev->dev.of_node;
	int ret;

	t->vadc_dev = qpnp_get_vadc(&pdev->dev, "thermal");
	if (IS_ERR(t->vadc_dev)) {
		ret = PTR_ERR(t->vadc_dev);
		if (ret != -EPROBE_DEFER)
			pr_err("VADC property missing\n");
		return ret;
	}

	ret = OF_READ_U32(node, "qcom,adc-channel", t->adc_chan);
	if (ret)
		return ret;

	ret = OF_READ_U32(node, "qcom,poll-ms", t->poll_jiffies);
	if (ret)
		return ret;

	/* Specifying a start delay is optional */
	OF_READ_U32(node, "qcom,start-delay", t->start_delay);

	/* Convert polling milliseconds to jiffies */
	t->poll_jiffies = msecs_to_jiffies(t->poll_jiffies);

	/* Calculate the number of zones */
	for_each_child_of_node(node, child)
		t->nr_zones++;

	if (!t->nr_zones) {
		pr_err("No zones specified\n");
		return -EINVAL;
	}

	t->zones = kmalloc(t->nr_zones * sizeof(*t->zones), GFP_KERNEL);
	if (!t->zones)
		return -ENOMEM;

	for_each_child_of_node(node, child) {
		struct thermal_zone *zone;
		u32 reg;

		ret = OF_READ_U32(child, "reg", reg);
		if (ret)
			goto free_zones;

		zone = t->zones + reg;

		ret = OF_READ_U32(child, "qcom,silver-khz", zone->silver_khz);
		if (ret)
			goto free_zones;

		ret = OF_READ_U32(child, "qcom,gold-khz", zone->gold_khz);
		if (ret)
			goto free_zones;

		ret = OF_READ_U32(child, "qcom,trip-deg", zone->trip_deg);
		if (ret)
			goto free_zones;
	}

	return 0;

free_zones:
	kfree(t->zones);
	return ret;
}

static int msm_thermal_simple_probe(struct platform_device *pdev)
{
	struct thermal_drv *t;
	int ret;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	t->wq = alloc_workqueue("msm_thermal_simple",
				WQ_HIGHPRI | WQ_UNBOUND, 0);
	if (!t->wq) {
		ret = -ENOMEM;
		goto free_t;
	}

	ret = msm_thermal_simple_parse_dt(pdev, t);
	if (ret)
		goto destroy_wq;

	/* Set the priority to INT_MIN so throttling can't be tampered with */
	t->cpu_notif.notifier_call = cpu_notifier_cb;
	t->cpu_notif.priority = INT_MIN;
	ret = cpufreq_register_notifier(&t->cpu_notif, CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		pr_err("Failed to register cpufreq notifier, err: %d\n", ret);
		goto free_zones;
	}

	/* Fire up the persistent worker */
	INIT_DELAYED_WORK(&t->throttle_work, thermal_throttle_worker);
	queue_delayed_work(t->wq, &t->throttle_work, t->start_delay * HZ);

	return 0;

free_zones:
	kfree(t->zones);
destroy_wq:
	destroy_workqueue(t->wq);
free_t:
	kfree(t);
	return ret;
}

static const struct of_device_id msm_thermal_simple_match_table[] = {
	{ .compatible = "qcom,msm-thermal-simple" },
	{ }
};

static struct platform_driver msm_thermal_simple_device = {
	.probe = msm_thermal_simple_probe,
	.driver = {
		.name = "msm-thermal-simple",
		.owner = THIS_MODULE,
		.of_match_table = msm_thermal_simple_match_table
	}
};

static int __init msm_thermal_simple_init(void)
{
	return platform_driver_register(&msm_thermal_simple_device);
}
device_initcall(msm_thermal_simple_init);
