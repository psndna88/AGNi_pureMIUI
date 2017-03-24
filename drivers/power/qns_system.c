/*
 * Copyright (C) 2014 Miro Zmrzli <miro@qnovocorp.com>
 * Copyright (C) 2014 Qnovo Corp
 * Copyright (C) 2016 Sony Mobile Communications Inc.
 * Copyright (C) 2016 Balázs Triszka <balika011@protonmail.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/alarmtimer.h>

static struct power_supply * battery_psy = NULL;

static struct alarm alarm;
static bool alarm_inited = false;
static int alarm_value = 0;

static struct wake_lock wakelock;
static bool wakelock_inited = false;
static bool wakelock_held = false;

static struct wake_lock charge_wakelock;
static bool charge_wakelock_inited = false;
static bool charge_wakelock_held = false;

static int options = -1;

static ssize_t qns_param_show(struct class *dev,
				struct class_attribute *attr,
				char *buf);

static ssize_t qns_param_store(struct class *dev,
		struct class_attribute *attr,
        const char *buf,
        size_t count);

enum
{
	IS_CHARGING,
	CURRENT,
	VOLTAGE,
	TEMPERATURE,
	FCC,
	DESIGN,
	SOC,
	BATTERY_TYPE,
	CHARGE_CURRENT,
	ALARM,
	OPTIONS,
};

static struct class_attribute qns_attrs[] = {
	__ATTR(charging_state, S_IRUGO, qns_param_show, NULL),
	__ATTR(current_now, S_IRUGO, qns_param_show, NULL),
	__ATTR(voltage, S_IRUGO, qns_param_show, NULL),
	__ATTR(temp, S_IRUGO, qns_param_show, NULL),
	__ATTR(fcc, S_IRUGO, qns_param_show, NULL),
	__ATTR(design, S_IRUGO, qns_param_show, NULL),
	__ATTR(soc, S_IRUGO, qns_param_show, NULL),
	__ATTR(battery_type, S_IRUGO, qns_param_show, NULL),
	__ATTR(charge_current, S_IWUSR, NULL, qns_param_store),
	__ATTR(alarm, S_IWUSR | S_IRUGO, qns_param_show, qns_param_store),
	__ATTR(options, S_IWUSR | S_IRUGO, qns_param_show, qns_param_store),
	__ATTR_NULL,
};
	
union power_supply_propval battery_get_property(int prop)
{
	union power_supply_propval propVal = {0, };

	if(battery_psy == NULL) {
		battery_psy = power_supply_get_by_name("battery");
		if(battery_psy == NULL) {
			pr_info("QNS: ERROR: unable to get \"battery\". Can't set the current!\n");
			return propVal;
		}
	}

	if(battery_psy->get_property(battery_psy, prop,
			&propVal) != 0) { \
		pr_info("QNS: ERROR: unable to read charger properties! Does \"battery\" have "
				"that property?\n");
		return propVal;
	}

	return propVal;
}

static ssize_t qns_param_show(struct class *dev,
				struct class_attribute *attr,
				char *buf)
{
	const ptrdiff_t off = attr - qns_attrs;

	switch(off)
	{
	case IS_CHARGING:
		return scnprintf(buf, PAGE_SIZE, "%d\n", battery_get_property(POWER_SUPPLY_PROP_STATUS).intval == POWER_SUPPLY_STATUS_CHARGING ? 1 : 0);
	case CURRENT:
		return scnprintf(buf, PAGE_SIZE, "%d\n", battery_get_property(POWER_SUPPLY_PROP_CURRENT_NOW).intval / 1000);
	case VOLTAGE:
		return scnprintf(buf, PAGE_SIZE, "%d\n", battery_get_property(POWER_SUPPLY_PROP_VOLTAGE_NOW).intval / 1000);
	case TEMPERATURE:
		return scnprintf(buf, PAGE_SIZE, "%d\n", battery_get_property(POWER_SUPPLY_PROP_TEMP).intval);
	case FCC:
		return scnprintf(buf, PAGE_SIZE, "%d\n", battery_get_property(POWER_SUPPLY_PROP_CHARGE_FULL).intval / 1000);
	case DESIGN:
		return scnprintf(buf, PAGE_SIZE, "%d\n", battery_get_property(POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN).intval / 1000);
	case SOC:
		return scnprintf(buf, PAGE_SIZE, "%d\n", battery_get_property(POWER_SUPPLY_PROP_CAPACITY).intval);
	case BATTERY_TYPE:
		return scnprintf(buf, PAGE_SIZE, "%s\n", battery_get_property(POWER_SUPPLY_PROP_BATTERY_TYPE).strval);
	case ALARM:
		return scnprintf(buf, PAGE_SIZE, "%d\n", alarm_value);
	case OPTIONS:
		return scnprintf(buf, PAGE_SIZE, "%d\n", options);
	}

	return 0;
}

static enum alarmtimer_restart qns_alarm_handler(struct alarm * alarm, ktime_t now)
{
	pr_info("QNS: ALARM! System wakeup!\n");
	wake_lock(&wakelock);
	wakelock_held = true;
	alarm_value = 1;
	return ALARMTIMER_NORESTART;
}

enum alarm_values
{
	CHARGE_WAKELOCK = -4,
	CHARGE_WAKELOCK_RELEASE = -3,
	HANDLED = -2,
	CANCEL = -1,
	IMMEDIATE = 0,
};

static ssize_t qns_param_store(struct class *dev,
		struct class_attribute *attr,
        const char *buf,
        size_t count)
{
	int val, ret = -EINVAL;
	ktime_t next_alarm;
	const ptrdiff_t off = attr - qns_attrs;
	
	if(battery_psy == NULL) {
		battery_psy = power_supply_get_by_name("battery");
		if(battery_psy == NULL) {
			pr_info("QNS: ERROR: unable to get \"battery\". Can't set the current!\n");
			return count;
		}
	}

	switch(off)
	{
	case CHARGE_CURRENT:
		ret = kstrtoint(buf, 10, &val);
		if (!ret && (val > 0)) {
			static int prev_ibat_for_deblog = -1;
			union power_supply_propval propVal = {val * 1000,};

			if (val != prev_ibat_for_deblog) {
				pr_info("QNS: new charge current:%d mA\n", val);
				if(battery_psy->set_property(battery_psy,
						POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
						&propVal) != 0) {
					pr_info("QNS: ERROR: unable to set charging current! Does \"battery\" have "
							"POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX property?\n");
					return count;
				}
				prev_ibat_for_deblog = val;
			}
		
			return count;
		}
		else
			return -EINVAL;
		break;

	case ALARM:
		ret = kstrtoint(buf, 10, &val);

		if(!wakelock_inited) {
			wake_lock_init(&wakelock, WAKE_LOCK_SUSPEND, "QnovoQNS");
			wakelock_inited = true;
		}

		if(!charge_wakelock_inited) {
			wake_lock_init(&charge_wakelock, WAKE_LOCK_SUSPEND, "QnovoQNSCharge");
			charge_wakelock_inited = true;
		}

		if (!ret)
		{
			switch(val)
			{
			case CHARGE_WAKELOCK:
				if(!charge_wakelock_held) {
					wake_lock(&charge_wakelock);
					charge_wakelock_held = true;
				}
				break;
			case CHARGE_WAKELOCK_RELEASE:
				if(charge_wakelock_held) {
					wake_unlock(&charge_wakelock);
					charge_wakelock_held = false;
				}
				break;
			case HANDLED:
				if(wakelock_held)
					wake_unlock(&wakelock);

				alarm_value = 0;
				wakelock_held = false;
				break;
			case CANCEL:
				if(alarm_inited)
					alarm_cancel(&alarm);

				alarm_value = 0;

				if(wakelock_held)
					wake_unlock(&wakelock);

				wakelock_held = false;
				break;
			case IMMEDIATE:
				if(!wakelock_held) {
					wake_lock(&wakelock);
					wakelock_held = true;
				}
				break;
			default:
				if(!alarm_inited) {
					alarm_init(&alarm, ALARM_REALTIME, qns_alarm_handler);
					alarm_inited = true;
				}

				next_alarm = ktime_set(val, 0);
				alarm_start_relative(&alarm, next_alarm);

				if(wakelock_held)
					wake_unlock(&wakelock);

				alarm_value = 0;
				wakelock_held = false;
			}
		}
		break;

	case OPTIONS:
		ret = kstrtoint(buf, 10, &val);
		if (!ret && (val >= 0))
			options = val;
		else
			return -EINVAL;

		break;
	}
	return count;
}

static struct class qns_class =
{
	.name = "qns",
	.owner = THIS_MODULE,
	.class_attrs = qns_attrs
};

static int qnovo_qns_init(void)
{
	class_register(&qns_class);
	return 0;
}
static void qnovo_qns_exit(void)
{
	class_unregister(&qns_class);
}

module_init(qnovo_qns_init);
module_exit(qnovo_qns_exit);

MODULE_AUTHOR("Miro Zmrzli <miro@qnovocorp.com>");
MODULE_DESCRIPTION("QNS System Driver v2");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("QNS");
