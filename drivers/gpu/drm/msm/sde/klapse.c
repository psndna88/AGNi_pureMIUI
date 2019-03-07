#include <linux/module.h>
#include <linux/init.h>
#include <uapi/linux/time.h>
#include <uapi/linux/rtc.h>
#include <linux/rtc.h>
#include <linux/timer.h>
#include "klapse.h"

#define LIC "GPLv2"
#define AUT "tanish2k09"
#define VER "4.3"

MODULE_LICENSE(LIC);
MODULE_AUTHOR(AUT);
MODULE_DESCRIPTION("A simple rgb dynamic lapsing module similar to livedisplay");
MODULE_VERSION(VER);


//Tunables :
static unsigned int daytime_r, daytime_g, daytime_b, target_r, target_g, target_b;
static unsigned int klapse_start_hour, klapse_stop_hour, enable_klapse;
static unsigned int brightness_factor_auto_start_hour, brightness_factor_auto_stop_hour;
static unsigned int brightness_factor;
static unsigned int backlight_lower, backlight_upper;
static unsigned int fadeback_minutes;
static unsigned int pulse_freq;
static bool brightness_factor_auto_enable;
static unsigned int target_minute; // <-- Masked as tunable by using klapse_scaling_rate sysfs name

/*
 *Internal calculation variables :
 *WARNING : DO NOT MAKE THEM TUNABLE
 */
static unsigned int b_cache;
static unsigned int current_r, current_g, current_b;
static unsigned int klapse_scaling_rate, active_minutes, last_bl;
static unsigned long local_time;
static struct rtc_time tm;
static struct timeval time;
static struct timer_list pulse_timer;

// Pulse prototype
static void klapse_pulse(unsigned long data);

//klapse related functions
static void restart_timer(void)
{
  mod_timer(&pulse_timer, jiffies + msecs_to_jiffies(pulse_freq));
  printk(KERN_INFO "KLapse pulse timer restarted!!!.\n");
}

static void flush_timer(void)
{
  if (timer_pending(&pulse_timer))
    mod_timer_pending(&pulse_timer, jiffies);
  printk(KERN_INFO "KLapse pulse timer flushed!!!.\n");
}

static void calc_active_minutes(void)
{
    bool isPulse = ((enable_klapse == 1) || (brightness_factor_auto_enable == 1));
    
    if (isPulse)
        flush_timer();
    
    if(klapse_start_hour > klapse_stop_hour)
        active_minutes = (24 + klapse_stop_hour - klapse_start_hour)*60;
    else
        active_minutes = (klapse_stop_hour - klapse_start_hour)*60;
        
    klapse_scaling_rate = (active_minutes*10)/target_minute;
    
    if (isPulse)
        klapse_pulse(0);
}

static int get_minutes_since_start(void)
{
    int hour, min;
    hour = tm.tm_hour - klapse_start_hour;

    if (hour < 0)
        hour += 24;

    min = ((hour*60) + tm.tm_min);
    return min;
}

static int get_minutes_before_stop(void)
{
    return (active_minutes - get_minutes_since_start());
}

static void set_rgb(int r, int g, int b)
{
    K_RED = r;
    K_GREEN = g;
    K_BLUE = b;
    
    current_r = r;
    current_g = g;
    current_b = b;
}

static void set_rgb_brightness(int r,int g,int b)
{

    r = ((r*brightness_factor)/10);
    g = ((g*brightness_factor)/10);
    b = ((b*brightness_factor)/10);

    if (r < 0)
        r = SCALE_VAL_MIN;
    else if (r > MAX_SCALE)
        r = MAX_SCALE;
    if (g < 0)
        g = SCALE_VAL_MIN;
    else if (g > MAX_SCALE)
        g = MAX_SCALE;
    if (b < 0)
        b = SCALE_VAL_MIN;
    else if (b > MAX_SCALE)
        b = MAX_SCALE;

    set_rgb(r,g,b);
}

static bool hour_within_range(int start, int stop, int check)
{
    // The 24-hour system is tricky because 0 comes after 23.
    // Handle it here properly

    // Check whether the start hour comes before stop hour.
    // if = 1, this would mean they are numerically in order
    // if both extremes are same, no time is possible inside so return false.
    // else, start hour is actually greater than stop, something like "From 5pm to 7am"
    // which translates to "From 17 to 7". It is clear why this could be a problem if not handled.
    if (start < stop) {
      if ((check >= start) && (check < stop))
        return 1;
      else
        return 0;
    }
    else if (start == stop)
      return 0;
    else {
      if ((check < stop) || (check >= start))
        return 1;
      else
        return 0;
    }
}
//klapse calc functions end here.

// klapse rgb update function
static void klapse_pulse(unsigned long data)
{
    int backtime;        
       
    // Get time
    do_gettimeofday(&time);
    local_time = (u32)(time.tv_sec - (sys_tz.tz_minuteswest * 60));
    rtc_time_to_tm(local_time, &tm);

    // Check brightness level automation
    if ((brightness_factor_auto_enable == 1) && !hour_within_range(brightness_factor_auto_start_hour, brightness_factor_auto_stop_hour, tm.tm_hour))
    {
        //Means not in dimmer-time
        brightness_factor = 10;
    }
    else
        brightness_factor = b_cache;

    // Check klapse automation, acts as a security measure too
    if (enable_klapse == 1)
    {
        backtime = get_minutes_before_stop();
    
        if(hour_within_range(klapse_start_hour, klapse_stop_hour, tm.tm_hour) == 0) //Means not in klapse time period.
        {
            set_rgb_brightness(daytime_r,daytime_g,daytime_b);
            if (!timer_pending(&pulse_timer))
              restart_timer();
            return;
        }
        else if (backtime > fadeback_minutes)
        {
            backtime = get_minutes_since_start();
            
            // For optimisation, this >= can be turned to an ==
            // But doing so will break reverse "time jumps" due to clock change
            // And a wrong RGB value will be calculated.                        
            if (backtime >= target_minute)
            {
              current_r = target_r;
              current_g = target_g;
              current_b = target_b;
            }
            else if (backtime < target_minute)
            {
              current_r = daytime_r - (((daytime_r - target_r)*backtime*klapse_scaling_rate)/(active_minutes*10));
              current_g = daytime_g - (((daytime_g - target_g)*backtime*klapse_scaling_rate)/(active_minutes*10));
              current_b = daytime_b - (((daytime_b - target_b)*backtime*klapse_scaling_rate)/(active_minutes*10));
            }
        }
        else
        {
            current_r = target_r + (((daytime_r - target_r)*(fadeback_minutes - backtime))/fadeback_minutes);
            current_g = target_g + (((daytime_g - target_g)*(fadeback_minutes - backtime))/fadeback_minutes);
            current_b = target_b + (((daytime_b - target_b)*(fadeback_minutes - backtime))/fadeback_minutes);           
        }
        
        set_rgb_brightness(current_r, current_g, current_b);
    }
    else
    {
        set_rgb_brightness(K_RED, K_GREEN, K_BLUE);
    }
    
    if (!timer_pending(&pulse_timer))
      restart_timer();
}

// Brightness-based mode
void set_rgb_slider(u32 bl_lvl)
{
  if (bl_lvl >= MIN_BRIGHTNESS)
  {
    if ((enable_klapse == 2) && (bl_lvl <= MAX_BRIGHTNESS))
    {
      if (bl_lvl > backlight_upper)
        set_rgb_brightness(daytime_r, daytime_g, daytime_b);
      else if (bl_lvl <= backlight_lower)
        set_rgb_brightness(target_r, target_g, target_b);
      else {
        current_r = daytime_r - ((daytime_r - target_r)*(backlight_upper - bl_lvl)/(backlight_upper - backlight_lower));
        current_g = daytime_g - ((daytime_g - target_g)*(backlight_upper - bl_lvl)/(backlight_upper - backlight_lower));
        current_b = daytime_b - ((daytime_b - target_b)*(backlight_upper - bl_lvl)/(backlight_upper - backlight_lower));
        set_rgb_brightness(current_r, current_g, current_b);
      }
    }
  
    last_bl = bl_lvl;
  }
}

static void set_enable_klapse(int val)
{
    if ((val <= 2) && (val >= 0))
    {
        if ((val == 1) && (enable_klapse != 1))
        {
          if (brightness_factor_auto_enable == 0)
          {
            klapse_pulse(0);
          }
        }
        else if (val == 0)
        {
            set_rgb_brightness(daytime_r, daytime_g, daytime_b);
            current_r = daytime_r;
            current_g = daytime_g;
            current_b = daytime_b;
            
            if (brightness_factor_auto_enable == 0)
              flush_timer();
        }
        else if (enable_klapse == 2)
            set_rgb_slider(last_bl);
        
        enable_klapse = val;
    }
}

//SYSFS node for details :
static ssize_t info_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "Author : %s\nVersion : %s\nLicense : %s\n", AUT, VER, LIC);

  return count;
}

static ssize_t info_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    return count;
}

//SYSFS tunables :
static ssize_t enable_klapse_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%d\n", enable_klapse);

  return count;
}

static ssize_t enable_klapse_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    int tmpval = 0;

    if (kstrtoint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    set_enable_klapse(tmpval);

    return count;
}

static ssize_t daytime_r_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", daytime_r);

  return count;
}

static ssize_t daytime_r_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        if (enable_klapse == 2)
        {
          daytime_r = tmpval;
          set_rgb_slider(last_bl);
        }
        else if (enable_klapse == 1)
        {
          flush_timer();
          daytime_r = tmpval;
          klapse_pulse(0);
        }
        else
          daytime_r = tmpval;
    }

    return count;
}

static ssize_t daytime_g_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", daytime_g);

  return count;
}

static ssize_t daytime_g_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        if (enable_klapse == 2)
        {
          daytime_g = tmpval;
          set_rgb_slider(last_bl);
        }
        else if (enable_klapse == 1)
        {
          flush_timer();
          daytime_g = tmpval;
          klapse_pulse(0);
        }
        else
          daytime_g = tmpval;
    }

    return count;
}

static ssize_t daytime_b_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", daytime_b);

  return count;
}

static ssize_t daytime_b_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        if (enable_klapse == 2)
        {
          daytime_b = tmpval;
          set_rgb_slider(last_bl);
        }
        else if (enable_klapse == 1)
        {
          flush_timer();
          daytime_b = tmpval;
          klapse_pulse(0);
        }
        else
          daytime_b = tmpval;
    }

    return count;
}

static ssize_t target_r_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", target_r);

  return count;
}

static ssize_t target_r_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        if (enable_klapse == 2)
        {
          target_r = tmpval;
          set_rgb_slider(last_bl);
        }
        else if (enable_klapse == 1)
        {
          flush_timer();
          target_r = tmpval;
          klapse_pulse(0);
        }
        else
          target_r = tmpval;
    }

    return count;
}

static ssize_t target_g_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", target_g);

  return count;
}

static ssize_t target_g_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        if (enable_klapse == 2)
        {
          target_g = tmpval;
          set_rgb_slider(last_bl);
        }
        else if (enable_klapse == 1)
        {
          flush_timer();
          target_g = tmpval;
          klapse_pulse(0);
        }
        else
          target_g = tmpval;
    }

    return count;
}

static ssize_t target_b_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", target_b);

  return count;
}

static ssize_t target_b_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        if (enable_klapse == 2)
        {
          target_b = tmpval;
          set_rgb_slider(last_bl);
        }
        else if (enable_klapse == 1)
        {
          flush_timer();
          target_b = tmpval;
          klapse_pulse(0);
        }
        else
          target_b = tmpval;
    }

    return count;
}

static ssize_t klapse_start_hour_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", klapse_start_hour);

  return count;
}

static ssize_t klapse_start_hour_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= 0) && (tmpval < 24) && (tmpval != klapse_stop_hour))
    {
        klapse_start_hour = tmpval;
        calc_active_minutes();
    }

    return count;
}

static ssize_t klapse_stop_hour_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", klapse_stop_hour);

  return count;
}

static ssize_t klapse_stop_hour_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= 0) && (tmpval < 24) && (tmpval != klapse_start_hour))
    {
        klapse_stop_hour = tmpval;
        calc_active_minutes();
    }

    return count;
}

static ssize_t klapse_scaling_rate_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", target_minute);

  return count;
}

static ssize_t klapse_scaling_rate_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval > 0) && (tmpval < active_minutes))
    {
        target_minute = tmpval;
        klapse_scaling_rate = (active_minutes*10)/target_minute;
    }

    return count;
}

static ssize_t brightness_factor_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", b_cache);

  return count;
}

static ssize_t brightness_factor_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= 2) && (tmpval <= 10) && (tmpval != b_cache))
    {
        // At this point, the brightness_factor could already have been changed, so to apply new brightness
        // the actual brightness RGB values must be restored.
        if (brightness_factor_auto_enable == 0) // The auto-brightness is already disabled, so simply reset actual RGB
        {
            if (enable_klapse == 1)         // Pulse is already running, for thread-safety, stop it and then modify RGB
            {
                flush_timer();
                set_rgb_brightness((K_RED*10)/b_cache, (K_GREEN*10)/b_cache, (K_BLUE*10)/b_cache);
                b_cache = tmpval;
                klapse_pulse(0);
            }
            else        // Means pulse isn't running
            {
                set_rgb_brightness((K_RED*10)/b_cache, (K_GREEN*10)/b_cache, (K_BLUE*10)/b_cache);
                b_cache = tmpval;
                brightness_factor = tmpval;
                if (enable_klapse == 2)
                    set_rgb_slider(last_bl);
                else
                    set_rgb_brightness(K_RED, K_GREEN, K_BLUE);
            }
        }
        else
        {
            flush_timer();
            set_rgb_brightness((K_RED*10)/brightness_factor, (K_GREEN*10)/brightness_factor, (K_BLUE*10)/brightness_factor);
            b_cache = tmpval;
            klapse_pulse(0);
        }
    }

    return count;
}


static ssize_t brightness_factor_auto_enable_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", brightness_factor_auto_enable);

  return count;
}

static ssize_t brightness_factor_auto_enable_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval == 0) || (tmpval == 1))
    {
        if (brightness_factor_auto_enable == tmpval) // Do nothing if the same value is entered
            return count;
        
        // At this point, the brightness_factor could already have been changed, so to apply new brightness
        // the actual brightness RGB values must be restored. Here, check whether the current RGB have been reduced :
        if (brightness_factor != 10)  // Guarantess that the brightness_factor was changed
        {
            if (brightness_factor_auto_enable == 0) // The auto-brightness is already disabled, so simply reset actual RGB. Also implies tmpval is 1
            {
                if (enable_klapse == 1)         // Pulse is already running, for thread-safety, stop it and then modify RGB
                    flush_timer();
                    
                set_rgb_brightness((K_RED*10)/b_cache, (K_GREEN*10)/b_cache, (K_BLUE*10)/b_cache);
                brightness_factor_auto_enable = tmpval;
                klapse_pulse(0);
                return count;
            }
            else    // Guarantees that pulse is on, and RGB is reduced, and tmpval is 0
            {
                flush_timer();
                set_rgb_brightness((K_RED*10)/brightness_factor, (K_GREEN*10)/brightness_factor, (K_BLUE*10)/brightness_factor);
                brightness_factor_auto_enable = tmpval;
                if (enable_klapse == 1)
                  klapse_pulse(0);
                else
                  set_rgb_brightness(K_RED, K_GREEN, K_BLUE);
                return count;
            }
        }
        else // if brightness_factor is 10, auto_enable could be within active range, or it could be off
        {
            brightness_factor_auto_enable = tmpval;
            
            if ((tmpval == 1) || (enable_klapse == 1)) // Force restart pulse, if it is to be used
            {
              flush_timer();
              klapse_pulse(0);
            }
            else if (tmpval == 0) // Stop pulse anyways
            {
                flush_timer();
                set_rgb_brightness(K_RED, K_GREEN, K_BLUE);
            }
        }
    }

    return count;
}

static ssize_t brightness_factor_auto_start_hour_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", brightness_factor_auto_start_hour);

  return count;
}

static ssize_t brightness_factor_auto_start_hour_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= 0) && (tmpval < 24) && (tmpval != brightness_factor_auto_stop_hour))
    {
        if ((brightness_factor_auto_enable == 1) || (enable_klapse == 1))
        {
            flush_timer();
            brightness_factor_auto_start_hour = tmpval;
            klapse_pulse(0);
        }
        else
            brightness_factor_auto_start_hour = tmpval;
    }

    return count;
}

static ssize_t brightness_factor_auto_stop_hour_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", brightness_factor_auto_stop_hour);

  return count;
}

static ssize_t brightness_factor_auto_stop_hour_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (kstrtouint(buf, 0, &tmpval) != 0)
        return -EINVAL;

    if ((tmpval >= 0) && (tmpval < 24) && (tmpval != brightness_factor_auto_start_hour))
    {
        if ((brightness_factor_auto_enable == 1) || (enable_klapse == 1))
        {
            flush_timer();
            brightness_factor_auto_stop_hour = tmpval;
            klapse_pulse(0);
        }
        else
            brightness_factor_auto_stop_hour = tmpval;
    }

    return count;
}

static ssize_t backlight_range_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u %u\n", backlight_lower, backlight_upper);

  return count;
}

static ssize_t backlight_range_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmp_l = 0, tmp_u = 0, tmp = 0;

    if (sscanf(buf, "%u %u", &tmp_l, &tmp_u) != 2)
      return -EINVAL;     

    if ((tmp_l >= MIN_BRIGHTNESS) && (tmp_l <= MAX_BRIGHTNESS) &&
        (tmp_u >= MIN_BRIGHTNESS) && (tmp_u <= MAX_BRIGHTNESS))
    {
        // Swap min and max correct
        if (tmp_u < tmp_l)
        {
          tmp = tmp_u;
          tmp_u = tmp_l;
          tmp_l = tmp;
        }
        
        backlight_lower = tmp_l;
        backlight_upper = tmp_u;
        
        if (enable_klapse == 2)
          set_rgb_slider(last_bl);
    }

    return count;
}

static ssize_t pulse_freq_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", pulse_freq);

  return count;
}

static ssize_t pulse_freq_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmp = 0;

    if (kstrtouint(buf, 0, &tmp) != 0)
        return -EINVAL;

    if ((tmp >= 1000) && (tmp <= 10*60000))
    {
        pulse_freq = tmp;
        flush_timer();
        klapse_pulse(0);
    }

    return count;
}

static ssize_t fadeback_minutes_show(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf)
{
  size_t count = 0;

  count += snprintf(buf, PAGE_SIZE, "%u\n", fadeback_minutes);

  return count;
}

static ssize_t fadeback_minutes_dump(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmp = 0;

    if (kstrtouint(buf, 0, &tmp) != 0)
        return -EINVAL;

    if ((tmp >= 0) && (tmp <= active_minutes))
    {
        if (enable_klapse == 1)
        {
            flush_timer();
            fadeback_minutes = tmp;
            klapse_pulse(0);
        }
        else
            fadeback_minutes = tmp;
    }

    return count;
}


static struct kobj_attribute enable_klapse_attribute =
	__ATTR(enable_klapse, 0644, enable_klapse_show, enable_klapse_dump);
static struct kobj_attribute daytime_r_attribute =
	__ATTR(daytime_r, 0644, daytime_r_show, daytime_r_dump);
static struct kobj_attribute daytime_g_attribute =
	__ATTR(daytime_g, 0644, daytime_g_show, daytime_g_dump);
static struct kobj_attribute daytime_b_attribute =
	__ATTR(daytime_b, 0644, daytime_b_show, daytime_b_dump);
static struct kobj_attribute target_r_attribute =
	__ATTR(target_r, 0644, target_r_show, target_r_dump);
static struct kobj_attribute target_g_attribute =
	__ATTR(target_g, 0644, target_g_show, target_g_dump);
static struct kobj_attribute target_b_attribute =
	__ATTR(target_b, 0644, target_b_show, target_b_dump);
static struct kobj_attribute klapse_start_hour_attribute =
	__ATTR(klapse_start_hour, 0644, klapse_start_hour_show, klapse_start_hour_dump);
static struct kobj_attribute klapse_stop_hour_attribute =
	__ATTR(klapse_stop_hour, 0644, klapse_stop_hour_show, klapse_stop_hour_dump);
static struct kobj_attribute klapse_scaling_rate_attribute =
	__ATTR(klapse_scaling_rate, 0644, klapse_scaling_rate_show, klapse_scaling_rate_dump);
static struct kobj_attribute brightness_factor_attribute =
	__ATTR(brightness_factor, 0644, brightness_factor_show, brightness_factor_dump);
static struct kobj_attribute brightness_factor_auto_attribute =
	__ATTR(brightness_factor_auto, 0644, brightness_factor_auto_enable_show, brightness_factor_auto_enable_dump);
static struct kobj_attribute brightness_factor_auto_start_hour_attribute =
	__ATTR(brightness_factor_auto_start_hour, 0644, brightness_factor_auto_start_hour_show, brightness_factor_auto_start_hour_dump);
static struct kobj_attribute brightness_factor_auto_stop_hour_attribute =
	__ATTR(brightness_factor_auto_stop_hour, 0644, brightness_factor_auto_stop_hour_show, brightness_factor_auto_stop_hour_dump);
static struct kobj_attribute backlight_range_attribute =
	__ATTR(backlight_range, 0644, backlight_range_show, backlight_range_dump);
static struct kobj_attribute pulse_freq_attribute =
	__ATTR(pulse_freq, 0644, pulse_freq_show, pulse_freq_dump);
static struct kobj_attribute fadeback_minutes_attribute =
	__ATTR(fadeback_minutes, 0644, fadeback_minutes_show, fadeback_minutes_dump);
static struct kobj_attribute info_attribute =
	__ATTR(info, 0444, info_show, info_dump);

static struct attribute *attrs[] = {
	&enable_klapse_attribute.attr,
	&daytime_r_attribute.attr,
	&daytime_g_attribute.attr,
	&daytime_b_attribute.attr,
	&target_r_attribute.attr,
	&target_g_attribute.attr,
	&target_b_attribute.attr,
	&klapse_start_hour_attribute.attr,
	&klapse_stop_hour_attribute.attr,
	&klapse_scaling_rate_attribute.attr,
	&brightness_factor_attribute.attr,
	&brightness_factor_auto_attribute.attr,
	&brightness_factor_auto_start_hour_attribute.attr,
	&brightness_factor_auto_stop_hour_attribute.attr,
	&backlight_range_attribute.attr,
	&pulse_freq_attribute.attr,
	&fadeback_minutes_attribute.attr,
	&info_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *klapse_kobj;

//INIT
static void values_setup(void)
{
    daytime_r = MAX_SCALE;
    daytime_g = MAX_SCALE;
    daytime_b = MAX_SCALE;
    current_r = MAX_SCALE;
    current_g = MAX_SCALE;
    current_b = MAX_SCALE;
    target_r = MAX_SCALE;
    target_g = (MAX_SCALE*79)/100;
    target_b = (MAX_SCALE*59)/100;
    brightness_factor = 10;
    b_cache = brightness_factor;
    target_minute = 300;
    klapse_start_hour = 17;
    klapse_stop_hour = 7;
    brightness_factor_auto_start_hour = 23;
    brightness_factor_auto_stop_hour = 6;
    enable_klapse = DEFAULT_ENABLE;
    brightness_factor_auto_enable = 0;
    backlight_lower = LOWER_BL_LVL;
    backlight_upper = UPPER_BL_LVL;
    last_bl = MAX_BRIGHTNESS;
    pulse_freq = 30000;
    fadeback_minutes = 60;
    calc_active_minutes();

    do_gettimeofday(&time);
    local_time = (u32)(time.tv_sec - (sys_tz.tz_minuteswest * 60));
    rtc_time_to_tm(local_time, &tm);
}

static int __init klapse_init(void)
{
    int rc;
    printk(KERN_INFO "KLapse init entered!!!.\n");
    
    values_setup();

    klapse_kobj = kobject_create_and_add("klapse", NULL);
    if (!klapse_kobj)
        return -ENOMEM;

    rc = sysfs_create_group(klapse_kobj, &attr_group);
    if (rc)
        pr_warn("%s: sysfs_create_group failed\n", __func__);
    
    setup_timer(&pulse_timer, klapse_pulse, 0);
    
    printk(KERN_INFO "KLapse init returning!!!.\n");

    return 0;
}

static void __exit klapse_exit(void){
    printk(KERN_INFO "KLapse exit entered!!!.\n");
    kobject_del(klapse_kobj);
    del_timer(&pulse_timer);
    printk(KERN_INFO "KLapse exit finished!!!.\n");
}

module_init(klapse_init);
module_exit(klapse_exit);
