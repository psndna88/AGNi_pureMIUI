/* Copyright (c) 2015, Varun Chitre <varun.chitre15@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Hexacore compatibility made by psndna88@xda
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/lcd_notify.h>
#include <linux/cpufreq.h>

static int suspend_cpu_num = 2, resume_cpu_num = 5;
static int device_cpus = 6;
static int core_limit = 6;

static int endurance_level = 0;

static bool isSuspended = false;

struct notifier_block lcd_worker;

#define DEBUG 0

#define THUNDERPLUG "thunderplug"

#define DRIVER_VERSION  2
#define DRIVER_SUBVER 5

#define CPU_LOAD_THRESHOLD        (40)

#define DEF_SAMPLING_MS			(400)

static int sampling_time = DEF_SAMPLING_MS;
static int load_threshold = CPU_LOAD_THRESHOLD;

static int tplug_hp_enabled = 1;

static int touch_boost_enabled = 0;

static struct workqueue_struct *tplug_wq;
static struct delayed_work tplug_work;

static struct workqueue_struct *tplug_boost_wq;
static struct delayed_work tplug_boost;

static struct workqueue_struct *tplug_resume_wq;
static struct delayed_work tplug_resume_work;

static unsigned int last_load[6] = {0, 0, 0, 0, 0, 0};

struct cpu_load_data {
	u64 prev_cpu_idle;
	u64 prev_cpu_wall;
	unsigned int avg_load_maxfreq;
	unsigned int cur_load_maxfreq;
	unsigned int samples;
	unsigned int window_size;
	cpumask_var_t related_cpus;
};

static DEFINE_PER_CPU(struct cpu_load_data, cpuload);

static inline void offline_cpus(void)
{
	unsigned int cpu;
	switch(endurance_level) {
		case 1: /* QuadCore mode */
			if(suspend_cpu_num > 4)
				suspend_cpu_num = 4;
		break;
		case 2: /* DualCore mode */
			if(suspend_cpu_num > 2)
				suspend_cpu_num = 2;
		break;
		default:
		break;
	}
	for(cpu = 5; cpu > (suspend_cpu_num - 1); cpu--) {
		if (cpu_online(cpu))
			cpu_down(cpu);
	}
	pr_info("%s: %d cpus were offlined\n", THUNDERPLUG, (device_cpus - suspend_cpu_num));
}

static inline void cpus_online_all(void)
{
	unsigned int cpu;
	switch(endurance_level) {
	case 1: /* QuadCore mode */
		if(resume_cpu_num > 3 || resume_cpu_num == 1)
			resume_cpu_num = 3;
	break;
	case 2: /* DualCore mode */
		if(resume_cpu_num > 1)
			resume_cpu_num = 1;
	break;
	case 0: /* Default mode */
		if(resume_cpu_num < 5)
			resume_cpu_num = 5;
	break;
	default:
	break;
	}

	for (cpu = 1; cpu <= resume_cpu_num; cpu++) {
		if (cpu_is_offline(cpu))
			cpu_up(cpu);
	}

	pr_info("%s: all cpus were onlined\n", THUNDERPLUG);
}

static void __ref tplug_boost_work_fn(struct work_struct *work)
{
	int cpu;
	for(cpu = 1; cpu < 4; cpu++) {
		if(cpu_is_offline(cpu))
			cpu_up(cpu);
	}
}

static void tplug_input_event(struct input_handle *handle, unsigned int type,
		unsigned int code, int value)
{

	if ((type == EV_KEY) && (code == BTN_TOUCH) && (value == 1)
		&& touch_boost_enabled == 1 && tplug_hp_enabled == 1)
	{
		if(DEBUG)
			pr_info("%s : touch boost\n", THUNDERPLUG);
		queue_delayed_work_on(0, tplug_boost_wq, &tplug_boost,
			msecs_to_jiffies(0));
	}
}

static int tplug_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpufreq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void tplug_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id tplug_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler tplug_input_handler = {
	.event          = tplug_input_event,
	.connect        = tplug_input_connect,
	.disconnect     = tplug_input_disconnect,
	.name           = "tplug_handler",
	.id_table       = tplug_ids,
};

static ssize_t thunderplug_suspend_cpus_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", suspend_cpu_num);
}

static ssize_t thunderplug_suspend_cpus_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	if(val < 1 || val > 6)
		pr_info("%s: suspend cpus off-limits\n", THUNDERPLUG);
	else
		suspend_cpu_num = val;

	return count;
}

static ssize_t thunderplug_endurance_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", endurance_level);
}

static ssize_t __ref thunderplug_endurance_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	switch(val) {
	case 0:
	case 1:
	case 2:
		if(endurance_level!=val) {
		endurance_level = val;
		offline_cpus();
		cpus_online_all();
	}
	break;
	default:
		pr_info("%s: invalid endurance level\n", THUNDERPLUG);
	break;
	}

	return count;
}

static ssize_t thunderplug_sampling_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", sampling_time);
}

static ssize_t __ref thunderplug_sampling_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	if(val > 50)
		sampling_time = val;

	return count;
}

static ssize_t thunderplug_hp_enabled_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", tplug_hp_enabled);
}

static ssize_t __ref thunderplug_hp_enabled_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	int last_val = tplug_hp_enabled;
	sscanf(buf, "%d", &val);
	switch(val)
	{
		case 0:
		case 1:
			tplug_hp_enabled = val;
		break;
		default:
			pr_info("%s : invalid choice\n", THUNDERPLUG);
		break;
	}

	if(tplug_hp_enabled == 1 && tplug_hp_enabled != last_val)
		queue_delayed_work_on(0, tplug_wq, &tplug_work,
							msecs_to_jiffies(sampling_time));

	return count;
}

static ssize_t thunderplug_tb_enabled_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", touch_boost_enabled);
}

static ssize_t __ref thunderplug_tb_enabled_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	switch(val)
	{
		case 0:
		case 1:
			touch_boost_enabled = val;
		break;
		default:
			pr_info("%s : invalid choice\n", THUNDERPLUG);
		break;
	}

	return count;
}

static ssize_t thunderplug_load_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", load_threshold);
}

static ssize_t __ref thunderplug_load_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	if(val > 10)
		load_threshold = val;

	return count;
}

static unsigned int get_curr_load(unsigned int cpu)
{
	int ret;
	unsigned int idle_time, wall_time;
	unsigned int cur_load;
	u64 cur_wall_time, cur_idle_time;
	struct cpu_load_data *pcpu = &per_cpu(cpuload, cpu);
	struct cpufreq_policy policy;

	ret = cpufreq_get_policy(&policy, cpu);
	if (ret)
		return -EINVAL;

	cur_idle_time = get_cpu_idle_time(cpu, &cur_wall_time, 0);

	wall_time = (unsigned int) (cur_wall_time - pcpu->prev_cpu_wall);
	pcpu->prev_cpu_wall = cur_wall_time;

	idle_time = (unsigned int) (cur_idle_time - pcpu->prev_cpu_idle);
	pcpu->prev_cpu_idle = cur_idle_time;

	if (unlikely(!wall_time || wall_time < idle_time))
		return 0;

	cur_load = 100 * (wall_time - idle_time) / wall_time;
	return cur_load;
}

static void thunderplug_suspend(void)
{
	offline_cpus();

	pr_info("%s: suspend\n", THUNDERPLUG);
}

static void __ref thunderplug_resume(void)
{
	cpus_online_all();

	pr_info("%s: resume\n", THUNDERPLUG);
}

static void __cpuinit tplug_resume_work_fn(struct work_struct *work)
{
	thunderplug_resume();
}

static void __cpuinit tplug_work_fn(struct work_struct *work)
{
	int i;
	unsigned int load[6], avg_load[6];

	switch(endurance_level)
	{
	case 0: /* Default mode */
		core_limit = 6;
	break;
	case 1: /* QuadCore mode */
		core_limit = 4;
	break;
	case 2: /* DualCore mode */
		core_limit = 2;
	break;
	default:
		core_limit = 6;
	break;
	}

	for(i = 0 ; i < core_limit; i++)
	{
		if(cpu_online(i))
			load[i] = get_curr_load(i);
		else
			load[i] = 0;

		avg_load[i] = ((int) load[i] + (int) last_load[i]) / 2;
		last_load[i] = load[i];
	}

	for(i = 0 ; i < core_limit; i++)
	{
	if(cpu_online(i) && avg_load[i] > load_threshold && cpu_is_offline(i+1))
	{
	if(DEBUG)
		pr_info("%s : bringing back cpu%d\n", THUNDERPLUG,i);
		if(!((i+1) > 5))
			cpu_up(i+1);
	}
	else if(cpu_online(i) && avg_load[i] < load_threshold && cpu_online(i+1))
	{
	if(DEBUG)
		pr_info("%s : offlining cpu%d\n", THUNDERPLUG,i);
		if(!(i+1)==0)
			cpu_down(i+1);
	}
	}

	if(tplug_hp_enabled != 0 && !isSuspended)
		queue_delayed_work_on(0, tplug_wq, &tplug_work,
			msecs_to_jiffies(sampling_time));
	else {
		if(!isSuspended)
			cpus_online_all();
		else
			thunderplug_suspend();
	}

}

static int lcd_notifier_callback(struct notifier_block *nb,
                                 unsigned long event, void *data)
{
       switch (event) {
       case LCD_EVENT_ON_START:
			isSuspended = false;
			if(tplug_hp_enabled)
				queue_delayed_work_on(0, tplug_wq, &tplug_work,
								msecs_to_jiffies(sampling_time));
			else
				queue_delayed_work_on(0, tplug_resume_wq, &tplug_resume_work,
		                      msecs_to_jiffies(10));
			pr_info("thunderplug : resume called\n");
               break;
       case LCD_EVENT_ON_END:
               break;
       case LCD_EVENT_OFF_START:
               break;
       case LCD_EVENT_OFF_END:
			isSuspended = true;
			pr_info("thunderplug : suspend called\n");
               break;
       default:
               break;
       }

       return 0;
}

static ssize_t thunderplug_ver_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
       return sprintf(buf, "ThunderPlug %u.%u", DRIVER_VERSION, DRIVER_SUBVER);
}

static struct kobj_attribute thunderplug_ver_attribute =
       __ATTR(version,
               0444,
               thunderplug_ver_show, NULL);

static struct kobj_attribute thunderplug_suspend_cpus_attribute =
       __ATTR(suspend_cpus,
               0666,
               thunderplug_suspend_cpus_show, thunderplug_suspend_cpus_store);

static struct kobj_attribute thunderplug_endurance_attribute =
       __ATTR(endurance_level,
               0666,
               thunderplug_endurance_show, thunderplug_endurance_store);

static struct kobj_attribute thunderplug_sampling_attribute =
       __ATTR(sampling_rate,
               0666,
               thunderplug_sampling_show, thunderplug_sampling_store);

static struct kobj_attribute thunderplug_load_attribute =
       __ATTR(load_threshold,
               0666,
               thunderplug_load_show, thunderplug_load_store);

static struct kobj_attribute thunderplug_hp_enabled_attribute =
       __ATTR(hotplug_enabled,
               0666,
               thunderplug_hp_enabled_show, thunderplug_hp_enabled_store);

static struct kobj_attribute thunderplug_tb_enabled_attribute =
       __ATTR(touch_boost,
               0666,
               thunderplug_tb_enabled_show, thunderplug_tb_enabled_store);

static struct attribute *thunderplug_attrs[] =
    {
        &thunderplug_ver_attribute.attr,
        &thunderplug_suspend_cpus_attribute.attr,
        &thunderplug_endurance_attribute.attr,
        &thunderplug_sampling_attribute.attr,
        &thunderplug_load_attribute.attr,
        &thunderplug_hp_enabled_attribute.attr,
        &thunderplug_tb_enabled_attribute.attr,
        NULL,
    };

static struct attribute_group thunderplug_attr_group =
    {
        .attrs = thunderplug_attrs,
    };

static struct kobject *thunderplug_kobj;

static int __init thunderplug_init(void)
{
        int ret = 0;
        int sysfs_result;
        printk(KERN_DEBUG "[%s]\n",__func__);

        thunderplug_kobj = kobject_create_and_add("thunderplug", kernel_kobj);

        if (!thunderplug_kobj) {
                pr_err("%s Interface create failed!\n",
                        __FUNCTION__);
                return -ENOMEM;
        }

        sysfs_result = sysfs_create_group(thunderplug_kobj, &thunderplug_attr_group);

        if (sysfs_result) {
                pr_info("%s sysfs create failed!\n", __FUNCTION__);
                kobject_put(thunderplug_kobj);
        }

		lcd_worker.notifier_call = lcd_notifier_callback;

        lcd_register_client(&lcd_worker);

		pr_info("%s : registering input boost", THUNDERPLUG);
		ret = input_register_handler(&tplug_input_handler);
		if (ret) {
		pr_err("%s: Failed to register input handler: %d\n",
		       THUNDERPLUG, ret);
		}

		tplug_wq = alloc_workqueue("tplug",
				WQ_HIGHPRI | WQ_UNBOUND, 1);

		tplug_resume_wq = alloc_workqueue("tplug_resume",
				WQ_HIGHPRI | WQ_UNBOUND, 1);

		tplug_boost_wq = alloc_workqueue("tplug_boost",
				WQ_HIGHPRI | WQ_UNBOUND, 1);

		INIT_DELAYED_WORK(&tplug_work, tplug_work_fn);
		INIT_DELAYED_WORK(&tplug_resume_work, tplug_resume_work_fn);
		INIT_DELAYED_WORK(&tplug_boost, tplug_boost_work_fn);
		queue_delayed_work_on(0, tplug_wq, &tplug_work,
		                      msecs_to_jiffies(10));

        pr_info("%s: init\n", THUNDERPLUG);

        return ret;
}

static void __exit thunderplug_exit(void)
{
	cancel_delayed_work(&tplug_work);
	cancel_delayed_work(&tplug_resume_work);
	cancel_delayed_work(&tplug_boost);
	flush_workqueue(tplug_wq);
	lcd_unregister_client(&lcd_worker);
	pr_info("%s : unregistering input boost", THUNDERPLUG);
	input_unregister_handler(&tplug_input_handler);
	kobject_put(thunderplug_kobj);
	sysfs_remove_group(thunderplug_kobj, &thunderplug_attr_group);
}

late_initcall(thunderplug_init);
module_exit(thunderplug_exit);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Varun Chitre <varun.chitre15@gmail.com>");
MODULE_DESCRIPTION("Hotplug driver for HexaCore CPU");
