#include <linux/module.h>
#include <linux/init.h>
#include <uapi/linux/time.h>
#include <uapi/linux/rtc.h>
#include <linux/rtc.h>
#include <linux/timer.h>
#include "klapse.h"

//Add additional headers below here only
#include "sde_hw_color_proc_v4.h"

/* DEFAULT_ENABLE values :
 * 0 = off
 * 1 = time-based scaling
 * 2 = brightness-based scaling
 */
#define DEFAULT_ENABLE  0

// MAX_SCALE : Maximum value of RGB possible
#define MAX_SCALE       256

// SCALE_VAL_MIN : Minimum value of RGB recommended
#define SCALE_VAL_MIN   20

// MAX_BRIGHTNESS : Maximum value of the display brightness/backlight
#define MAX_BRIGHTNESS  1023

// MIN_BRIGHTNESS : Minimum value of the display brightness/backlight
#define MIN_BRIGHTNESS  2

/* UPPER_BL_LVL : Initial upper limit for brightness-dependent mode. 
 * Value <= MAX_BRIGHTNESS && > LOWER_BL_LVL (MUST)
 */
#define UPPER_BL_LVL  200

/* LOWER_BL_LVL : Initial lower limit for brightness-dependent mode. 
 * Value < UPPER_BL_LVL (MUST)
 */
#define LOWER_BL_LVL 2

#define LIC "GPLv2"
#define AUT "tanish2k09"
#define VER "4.2"

MODULE_LICENSE(LIC);
MODULE_AUTHOR(AUT);
MODULE_DESCRIPTION("A simple rgb dynamic lapsing module similar to livedisplay");
MODULE_VERSION(VER);


//Tunables :
static unsigned int daytime_r, daytime_g, daytime_b, target_r, target_g, target_b;
static unsigned int klapse_start_hour, klapse_stop_hour, enable_klapse;
static unsigned int brightness_factor_auto_start_hour, brightness_factor_auto_stop_hour;
static unsigned int klapse_scaling_rate, brightness_factor;
static unsigned int backlight_lower, backlight_upper;
static unsigned int fadeback_minutes;
static unsigned int pulse_freq;
static bool brightness_factor_auto_enable;

/*
 *Internal calculation variables :
 *WARNING : DO NOT MAKE THEM TUNABLE
 */
static int target_minute;
static unsigned int b_cache;
static unsigned int current_r, current_g, current_b;
static unsigned int active_minutes, last_bl;
static unsigned long local_time;
static struct rtc_time tm;
static struct timeval time;
static struct timer_list pulse_timer;


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
    if(klapse_start_hour > klapse_stop_hour)
        active_minutes = (24 + klapse_stop_hour - klapse_start_hour)*60;
    else
        active_minutes = (klapse_stop_hour - klapse_start_hour)*60;
        
    klapse_scaling_rate = (active_minutes*10)/target_minute;
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

    // Check klapse automation, and a security measure too
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
static ssize_t info_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "Author : %s\nVersion : %s\nLicense : %s\n", AUT, VER, LIC);

  return count;
}

static ssize_t info_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    return count;
}

//SYSFS tunables :
static ssize_t enable_klapse_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%d\n", enable_klapse);

  return count;
}

static ssize_t enable_klapse_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    int tmpval = 0;

    if (!sscanf(buf, "%d", &tmpval))
      return -EINVAL;

    set_enable_klapse(tmpval);

    return count;
}

static ssize_t daytime_r_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", daytime_r);

  return count;
}

static ssize_t daytime_r_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        daytime_r = tmpval;
        if (enable_klapse == 0)
          set_rgb_brightness(daytime_r, daytime_g, daytime_b);
        else if (enable_klapse == 2)
          set_rgb_slider(last_bl);
        else if (enable_klapse == 1)
        {
          flush_timer();
          klapse_pulse(0);
        }
    }

    return count;
}

static ssize_t daytime_g_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", daytime_g);

  return count;
}

static ssize_t daytime_g_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
        return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        daytime_g = tmpval;
        if (enable_klapse == 0)
           set_rgb_brightness(daytime_r, daytime_g, daytime_b);
        else if (enable_klapse == 2)
           set_rgb_slider(last_bl);
        else if (enable_klapse == 1)
        {
          flush_timer();
          klapse_pulse(0);
        }
    }

    return count;
}

static ssize_t daytime_b_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", daytime_b);

  return count;
}

static ssize_t daytime_b_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
        return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        daytime_b = tmpval;
        if (enable_klapse == 0)
           set_rgb_brightness(daytime_r, daytime_g, daytime_b);
        else if (enable_klapse == 2)
           set_rgb_slider(last_bl);
        else if (enable_klapse == 1)
        {
          flush_timer();
          klapse_pulse(0);
        }
    }

    return count;
}

static ssize_t target_r_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", target_r);

  return count;
}

static ssize_t target_r_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        target_r = tmpval;
        if (enable_klapse == 2)
          set_rgb_slider(last_bl);
        else if (enable_klapse == 1)
        {
          flush_timer();
          klapse_pulse(0);
        }
    }

    return count;
}

static ssize_t target_g_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", target_g);

  return count;
}

static ssize_t target_g_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        target_g = tmpval;
        if (enable_klapse == 2)
          set_rgb_slider(last_bl);
        else if (enable_klapse == 1)
        {
          flush_timer();
          klapse_pulse(0);
        }
    }

    return count;
}

static ssize_t target_b_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", target_b);

  return count;
}

static ssize_t target_b_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval >= (SCALE_VAL_MIN)) && (tmpval <= MAX_SCALE))
    {
        target_b = tmpval;
        if (enable_klapse == 2)
          set_rgb_slider(last_bl);
        else if (enable_klapse == 1)
        {
          flush_timer();
          klapse_pulse(0);
        }
    }

    return count;
}

static ssize_t klapse_start_hour_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", klapse_start_hour);

  return count;
}

static ssize_t klapse_start_hour_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval >= 0) && (tmpval < 24) && (tmpval != klapse_stop_hour))
    {
        klapse_start_hour = tmpval;
        calc_active_minutes();
    }

    return count;
}

static ssize_t klapse_stop_hour_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", klapse_stop_hour);

  return count;
}

static ssize_t klapse_stop_hour_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval >= 0) && (tmpval < 24) && (tmpval != klapse_start_hour))
    {
        klapse_stop_hour = tmpval;
        calc_active_minutes();
    }

    return count;
}

static ssize_t klapse_scaling_rate_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", klapse_scaling_rate);

  return count;
}

static ssize_t klapse_scaling_rate_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval > 0) && (tmpval < active_minutes))
    {
        target_minute = tmpval;
        klapse_scaling_rate = (active_minutes*10)/target_minute;
    }

    return count;
}

static ssize_t brightness_factor_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", b_cache);

  return count;
}

static ssize_t brightness_factor_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval >= 2) && (tmpval <= 10))
    {
        if (brightness_factor_auto_enable == 0)
        {
          brightness_factor = tmpval;
          set_rgb_brightness((K_RED*10)/b_cache, (K_GREEN*10)/b_cache, (K_BLUE*10)/b_cache);
        }
        b_cache = tmpval;
    }

    return count;
}


static ssize_t brightness_factor_auto_enable_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", brightness_factor_auto_enable);

  return count;
}

static ssize_t brightness_factor_auto_enable_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval == 0) || (tmpval == 1))
    {
        if ((tmpval == 1) && hour_within_range(brightness_factor_auto_start_hour, brightness_factor_auto_stop_hour, tm.tm_hour))
          set_rgb_brightness(K_RED, K_GREEN, K_BLUE);
        if ((tmpval == 1) && (enable_klapse != 1) && (brightness_factor_auto_enable != 1))
        {
          klapse_pulse(0);
        }
        
        if ((tmpval == 0) && (enable_klapse == 0))
        {
          flush_timer();
        }
        brightness_factor_auto_enable = tmpval;
    }

    return count;
}

static ssize_t brightness_factor_auto_start_hour_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", brightness_factor_auto_start_hour);

  return count;
}

static ssize_t brightness_factor_auto_start_hour_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval >= 0) && (tmpval < 24) && (tmpval != brightness_factor_auto_stop_hour))
    {
        brightness_factor_auto_start_hour = tmpval;
        if ((brightness_factor_auto_enable == 1) && hour_within_range(brightness_factor_auto_start_hour, brightness_factor_auto_stop_hour, tm.tm_hour))
          set_rgb_brightness(K_RED, K_GREEN, K_BLUE);
    }

    return count;
}

static ssize_t brightness_factor_auto_stop_hour_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", brightness_factor_auto_stop_hour);

  return count;
}

static ssize_t brightness_factor_auto_stop_hour_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmpval = 0;

    if (!sscanf(buf, "%u", &tmpval))
      return -EINVAL;

    if ((tmpval >= 0) && (tmpval < 24) && (tmpval != brightness_factor_auto_start_hour))
    {
        brightness_factor_auto_stop_hour = tmpval;
        if ((brightness_factor_auto_enable == 1) && hour_within_range(brightness_factor_auto_start_hour, brightness_factor_auto_stop_hour, tm.tm_hour))
          set_rgb_brightness(K_RED, K_GREEN, K_BLUE);
    }

    return count;
}

static ssize_t backlight_range_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u %u\n", backlight_lower, backlight_upper);

  return count;
}

static ssize_t backlight_range_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
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

static ssize_t pulse_freq_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", pulse_freq);

  return count;
}

static ssize_t pulse_freq_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmp = 0;

    if (!sscanf(buf, "%u", &tmp))
      return -EINVAL;     

    if ((tmp >= 1000) && (tmp <= 10*60000))
    {
        pulse_freq = tmp;
        flush_timer();
        klapse_pulse(0);
    }

    return count;
}

static ssize_t fadeback_minutes_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  size_t count = 0;

  count += sprintf(buf, "%u\n", fadeback_minutes);

  return count;
}

static ssize_t fadeback_minutes_dump(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int tmp = 0;

    if (!sscanf(buf, "%u", &tmp))
      return -EINVAL;     

    if ((tmp >= 0) && (tmp <= active_minutes))
    {
        fadeback_minutes = tmp;
        flush_timer();
        klapse_pulse(0);
    }

    return count;
}


static DEVICE_ATTR(enable_klapse, 0644, enable_klapse_show, enable_klapse_dump);
static DEVICE_ATTR(daytime_r, 0644, daytime_r_show, daytime_r_dump);
static DEVICE_ATTR(daytime_g, 0644, daytime_g_show, daytime_g_dump);
static DEVICE_ATTR(daytime_b, 0644, daytime_b_show, daytime_b_dump);
static DEVICE_ATTR(target_r, 0644, target_r_show, target_r_dump);
static DEVICE_ATTR(target_g, 0644, target_g_show, target_g_dump);
static DEVICE_ATTR(target_b, 0644, target_b_show, target_b_dump);
static DEVICE_ATTR(klapse_start_hour, 0644, klapse_start_hour_show, klapse_start_hour_dump);
static DEVICE_ATTR(klapse_stop_hour, 0644, klapse_stop_hour_show, klapse_stop_hour_dump);
static DEVICE_ATTR(klapse_scaling_rate, 0644, klapse_scaling_rate_show, klapse_scaling_rate_dump);
static DEVICE_ATTR(brightness_factor, 0644, brightness_factor_show, brightness_factor_dump);
static DEVICE_ATTR(brightness_factor_auto, 0644, brightness_factor_auto_enable_show, brightness_factor_auto_enable_dump);
static DEVICE_ATTR(brightness_factor_auto_start_hour, 0644, brightness_factor_auto_start_hour_show, brightness_factor_auto_start_hour_dump);
static DEVICE_ATTR(brightness_factor_auto_stop_hour, 0644, brightness_factor_auto_stop_hour_show, brightness_factor_auto_stop_hour_dump);
static DEVICE_ATTR(backlight_range, 0644, backlight_range_show, backlight_range_dump);
static DEVICE_ATTR(pulse_freq, 0644, pulse_freq_show, pulse_freq_dump);
static DEVICE_ATTR(fadeback_minutes, 0644, fadeback_minutes_show, fadeback_minutes_dump);
static DEVICE_ATTR(info, 0444, info_show, info_dump);

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
    last_bl = 1023;
    pulse_freq = 30000;
    fadeback_minutes = 60;
    calc_active_minutes();

    do_gettimeofday(&time);
    local_time = (u32)(time.tv_sec - (sys_tz.tz_minuteswest * 60));
    rtc_time_to_tm(local_time, &tm);
}

struct kobject *klapse_kobj;
EXPORT_SYMBOL_GPL(klapse_kobj);

static int __init klapse_init(void)
{
    int rc;
    printk(KERN_INFO "KLapse init entered!!!.\n");
    
    values_setup();

    klapse_kobj = kobject_create_and_add("klapse", NULL) ;
    if (klapse_kobj == NULL) {
      pr_warn("%s: klapse_kobj create_and_add failed\n", __func__);
    }

    rc = sysfs_create_file(klapse_kobj, &dev_attr_info.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for info\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_enable_klapse.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for enable_klapse\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_daytime_r.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for daytime_r\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_daytime_g.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for daytime_g\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_daytime_b.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for daytime_b\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_target_r.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for target_r\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_target_g.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for target_g\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_target_b.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for target_b\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_klapse_start_hour.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for klapse_start_hour\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_klapse_stop_hour.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for klapse_stop_hour\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_klapse_scaling_rate.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for klapse_scaling_rate\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_brightness_factor.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for brightness_factor\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_brightness_factor_auto.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for brightness_factor_auto\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_brightness_factor_auto_start_hour.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for brightness_factor_auto_start_hour\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_brightness_factor_auto_stop_hour.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for brightness_factor_auto_stop_hour\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_backlight_range.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for backlight_range\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_pulse_freq.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for pulse_freq\n", __func__);
    }
    rc = sysfs_create_file(klapse_kobj, &dev_attr_fadeback_minutes.attr);
    if (rc) {
      pr_warn("%s: sysfs_create_file failed for fadeback_minutes\n", __func__);
    }
    
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
