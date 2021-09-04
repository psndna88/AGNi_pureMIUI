#include <sound/soc.h>
#include "tas25xx-calib.h"
#include <dsp/tas_smart_amp_v2.h>
#include <dsp/q6afe-v2.h>
#include <linux/syscalls.h>
#include <linux/fs.h>

/*Master Control to Bypass the Smartamp TI CAPIv2 module*/
static int s_tas_smartamp_bypass = 0;
static int s_tas_smartamp_enable = 0;
static int s_debug_enable = 0;
static int s_calib_test_flag = 0;
static int s_profile_id = 0;
static struct mutex routing_lock;

#ifdef CONFIG_SET_RE_IN_KERNEL
static int get_calibrated_re_tcalib(uint32_t *rdc_fix, uint32_t *tv_fix, int channel_count)
{
    struct file *file = NULL;
    char calib_data[MAX_STRING] = { 0 };
    char filepath[128] = SMARTAMP_SPEAKER_CALIBDATA_FILE;
    mm_segment_t fs;
    int ret = 0;

    static uint32_t s_rdc_fix[2] = { POISON_VAL, POISON_VAL };
    static uint32_t s_tv_fix = POISON_VAL;

#if USE_VFS
    loff_t pos = 0;
#else
    int size;
    int fd;
#endif

    if ((s_rdc_fix[0] == POISON_VAL) &&
        (s_rdc_fix[1] == POISON_VAL)
        )
    {
        fs = get_fs();
        set_fs(get_ds());
#if USE_VFS
        file = filp_open(filepath, O_RDONLY, 0);
        if (!IS_ERR(file))
        {
            vfs_read(file, calib_data, MAX_STRING - 1, &pos);

            if (channel_count == 1) {
                if (sscanf(calib_data, "%d;%d;", s_rdc_fix, &s_tv_fix) != 2) {
                    pr_err("TI-SmartPA: %s: file %s read error\n", __func__, filepath);
                    ret = -EIO;
                }
            }
            else if (channel_count == 2) {
                if (sscanf(calib_data, "%d;%d;%d;", &s_rdc_fix[0], &s_rdc_fix[1], &s_tv_fix) != 3) {
                    pr_err("TI-SmartPA: %s: file %s read error\n", __func__, filepath);
                    ret = -EIO;
                }
            }
            filp_close(file, NULL);
        }
        else {
            pr_err("TI-SmartPA: %s: file %s open failed %p \n ", __func__, filepath, file);
            ret = -EIO;
        }
#else
        fd = sys_open(filepath, O_RDONLY, 0);
        if (fd > 0) {
            memset(calib_data, 0, sizeof(calib_data));
            size = sys_read(fd, calib_data, MAX_STRING - 1);
            sys_close(fd);

            pr_info("TI-SmartPA: %s: *** sys_read size = %d\n", size);

            if (channel_count == 1) {
                if (sscanf(calib_data, "%d;%d;", &rdc_fix[0], tv_fix) != 2) {
                    pr_err("TI-SmartPA: %s: file %s read error\n", __func__, filepath);
                    ret = -EIO;
                }
            }
            else if (channel_count == 2) {
                if (sscanf(calib_data, "%d;%d;%d;", &rdc_fix[0], &rdc_fix[1], tv_fix) != 3) {
                    pr_err("TI-SmartPA: %s: file %s read error\n", __func__, filepath);
                    ret = -EIO;
                }
            }
        }
        else {
            pr_err("TI-SmartPA: %s: file %s open failed %p \n ", __func__, filepath, file);
            ret = -EIO;
        }
#endif /* USE_VFS */
        set_fs(fs);
    }

    if (ret == 0) {
        if (rdc_fix) {
            rdc_fix[0] = (uint32_t)s_rdc_fix[0];

            if (channel_count == 2) {
                rdc_fix[1] = (uint32_t)s_rdc_fix[1];
            }
        }

        if (tv_fix)
            *tv_fix = (uint32_t)s_tv_fix;
    }
    return ret;
}
#endif /* CONFIG_SET_RE_IN_KERNEL */

static int tas25xx_smartamp_get_set(u8 *user_data, uint32_t param_id,
    uint8_t get_set, uint32_t length, uint32_t module_id)
{
    int ret = 0;

    switch (get_set) {
    case TAS_SET_PARAM:
        if (s_tas_smartamp_bypass) {
            pr_err("TI-SmartPA: %s: SmartAmp is bypassed no control set\n", __func__);
            goto fail_cmd;
        }
        ret = afe_tas_smartamp_set_calib_data(module_id, param_id, length, user_data);
        break;

    case TAS_GET_PARAM:
        if (!s_tas_smartamp_bypass) {
            memset(user_data, 0, length);
            ret = afe_tas_smartamp_get_calib_data(module_id, param_id, length, user_data);
        }
        break;

    default:
        goto fail_cmd;
    }

fail_cmd:
    return ret;
}

/*Wrapper arround set/get parameter, all set/get commands pass through this wrapper*/
int tas25xx_smartamp_algo_ctrl(u8 *user_data, uint32_t param_id,
    uint8_t get_set, uint32_t length, uint32_t module_id)
{
    int ret = 0;
    mutex_lock(&routing_lock);
    ret = tas25xx_smartamp_get_set(user_data, param_id, get_set, length, module_id);
    mutex_unlock(&routing_lock);
    return ret;
}
EXPORT_SYMBOL(tas25xx_smartamp_algo_ctrl);

/*Control-1: Set Profile*/
static const char *profile_index_text[] = { "NONE", TAS_ALGO_PROFILE_LIST, "CALIB" };
static const struct soc_enum profile_index_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(profile_index_text), profile_index_text),
};

static int tas25xx_set_profile(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret;
    int profile_id = pUcontrol->value.integer.value[0];
    int param_id = 0;
    int max_number_of_profiles = sizeof(profile_index_text) / sizeof(profile_index_text[0]);
    if ((profile_id >= max_number_of_profiles) || (profile_id < 0))
        return -EINVAL;

    s_profile_id = profile_id;

    pr_info("TI-SmartPA: %s: Setting profile %s \n", __func__, profile_index_text[profile_id]);
    if (profile_id)
        profile_id -= 1;
    else
        return 0;

    param_id = TAS_CALC_PARAM_IDX(TAS_SA_SET_PROFILE, 1, CHANNEL0);
    pr_info("TI-SmartPA: %s: Sending set profile\n", __func__);
    ret = tas25xx_smartamp_algo_ctrl((u8 *)&profile_id, param_id,
        TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
    if (ret < 0)
        pr_err("TI-SmartPA: %s: Failed to set config\n", __func__);
    return ret;
}

static int tas25xx_get_profile(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret;
    int profile_id = 0;
    int param_id = 0;
    int max_number_of_profiles = sizeof(profile_index_text) / sizeof(profile_index_text[0]);

    if (s_tas_smartamp_enable && s_debug_enable) {
        param_id = TAS_CALC_PARAM_IDX(TAS_SA_SET_PROFILE, 1, CHANNEL0);
        ret = tas25xx_smartamp_algo_ctrl((u8 *)&profile_id, param_id,
            TAS_GET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
        if (ret < 0) {
            pr_err("TI-SmartPA: %s: Failed to get profile\n", __func__);
            profile_id = 0;
        }
        else {
            profile_id += 1;
        }
    }
    else
        profile_id = s_profile_id;

    pUcontrol->value.integer.value[0] = profile_id;

    if ((profile_id < max_number_of_profiles) && (profile_id > -1)) {
        pr_info("TI-SmartPA: %s: getting profile %s\n", __func__, profile_index_text[profile_id]);
    }

    return 0;
}

/*Control-2: Set Calibrated Rdc*/
static int tas25xx_set_Re_common(int re_value_in, int channel)
{
    int ret;
    int param_id = 0;
    int re_value = re_value_in;

    param_id = TAS_CALC_PARAM_IDX(TAS_SA_SET_RE, 1, channel);
    ret = tas25xx_smartamp_algo_ctrl((u8 *)&re_value, param_id,
        TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);

    return ret;
}

static int tas25xx_set_Re_left(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int re_value = pUcontrol->value.integer.value[0];

    pr_info("TI-SmartPA: %s: Setting Re %d", __func__, re_value);
    return tas25xx_set_Re_common(re_value, CHANNEL0);
}

static int tas25xx_dummy_get(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = 0;
    pUcontrol->value.integer.value[0] = 0;
    return ret;
}

static int tas25xx_dummy_set(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    return 0;
}

/*Control-3: Calibration and Test(F0,Q,Tv) Controls*/
static const char *tas25xx_calib_test_text[] = {
    "NONE",
    "CALIB_START",
    "CALIB_STOP",
    "TEST_START",
    "TEST_STOP"
};

static const struct soc_enum tas25xx_calib_test_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tas25xx_calib_test_text), tas25xx_calib_test_text),
};

static int tas25xx_calib_test_set_common(int calib_command, int channel)
{
    int ret = 0;
    int param_id = 0;
    int data = 1;

    switch (calib_command) {
    case CALIB_START:
        pr_info("TI-SmartPA: %s: CALIB_START", __func__);
        s_calib_test_flag = 1;
        param_id = TAS_CALC_PARAM_IDX(TAS_SA_CALIB_INIT, 1, channel);
        break;

    case CALIB_STOP:
        pr_info("TI-SmartPA: %s: CALIB_STOP", __func__);
        s_calib_test_flag = 0;
        param_id = TAS_CALC_PARAM_IDX(TAS_SA_CALIB_DEINIT, 1, channel);
        break;

    case TEST_START:
        s_calib_test_flag = 1;
        break;

    case TEST_STOP:
        s_calib_test_flag = 0;
        break;

    default:
        pr_info("TI-SmartPA: %s: no impl calib_command %d\n", __func__, calib_command);
        ret = -EINVAL;
        break;
    }

    if (param_id) {
        ret = tas25xx_smartamp_algo_ctrl((u8 *)&data, param_id,
            TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
        if (ret < 0) {
            s_calib_test_flag = 0;
            pr_err("TI-SmartPA: %s: Failed to set calib/test, ret=%d\n", __func__, ret);
        }
    }

    return ret;
}

static int tas25xx_calib_test_set(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = 0;

    int user_data = pUcontrol->value.integer.value[0];
    if ((0 == (ret = tas25xx_calib_test_set_common(user_data, CHANNEL0)))) {
#ifdef CONFIG_TAS25XX_ALGO_STEREO
        ret = tas25xx_calib_test_set_common(user_data, CHANNEL1);
#endif
    }

    return ret;
}

/*Control-4: Get Re*/
/*returns -ve error or +ve re value, 0 if not called*/
static int tas25xx_get_re_common(int channel)
{
    int ret = 0;
    int re_value = 0;
    int param_id = 0;

    pr_info("TI-SmartPA: %s, channel=%d\n", __func__, channel);

    if (s_tas_smartamp_enable && (s_calib_test_flag || s_debug_enable)) {
        param_id = TAS_CALC_PARAM_IDX(TAS_SA_GET_RE, 1, channel);
        ret = tas25xx_smartamp_algo_ctrl((u8 *)&re_value, param_id,
            TAS_GET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
        if (ret < 0) {
            pr_err("TI-SmartPA: %s: Failed to get Re\n", __func__);
        }
        else {
            ret = re_value;
        }
    }

    return ret;
}

static int tas25xx_get_re_left(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = tas25xx_get_re_common(CHANNEL0);
    if (ret >= 0) {
        pUcontrol->value.integer.value[0] = ret;
        ret = 0;
        pr_info("TI-SmartPA: %s: Getting Re %d\n", __func__, ret);
    }

    return ret;
}

/*Control-5: Get F0*/
static int tas25xx_get_f0_common(int channel)
{
    int f0_value = 0;
    int param_id = 0;
    int ret = 0;

    pr_info("TI-SmartPA: %s, channel=%d\n", __func__, channel);

    if (s_tas_smartamp_enable && (s_calib_test_flag || s_debug_enable)) {
        param_id = TAS_CALC_PARAM_IDX(TAS_SA_GET_F0, 1, channel);
        ret = tas25xx_smartamp_algo_ctrl((u8 *)&f0_value, param_id,
            TAS_GET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
        if (ret < 0) {
            pr_err("TI-SmartPA: %s: Failed to get F0\n", __func__);
        }
        else {
            ret = f0_value;
        }
    }

    return ret;
}

static int tas25xx_get_f0_left(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = tas25xx_get_f0_common(CHANNEL0);

    if (ret >= 0) {
        pUcontrol->value.integer.value[0] = ret;
        pr_info("TI-SmartPA: %s: Getting F0 val=%d\n", __func__, ret);
        ret = 0;
    }

    return ret;
}

/*Control-6: Get Q*/
static int tas25xx_get_q_common(int channel)
{
    int ret = 0;
    int q_value = 0;
    int param_id = 0;

    pr_info("TI-SmartPA: %s, channel=%d", __func__, channel);

    if (s_tas_smartamp_enable && (s_calib_test_flag || s_debug_enable)) {
        param_id = TAS_CALC_PARAM_IDX(TAS_SA_GET_Q, 1, channel);
        ret = tas25xx_smartamp_algo_ctrl((u8 *)&q_value, param_id,
            TAS_GET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
        if (ret < 0) {
            pr_err("TI-SmartPA: %s: Failed to get F0\n", __func__);
        }
        else {
            ret = q_value;
        }
    }

    return ret;
}

static int tas25xx_get_q_left(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = tas25xx_get_q_common(CHANNEL0);
    if (ret >= 0) {
        pUcontrol->value.integer.value[0] = ret;
        pr_info("TI-SmartPA: %s: Getting Q %d\n", __func__, ret);
        ret = 0;
    }

    return ret;
}

/*Control-7: Get Tv*/
static int tas25xx_get_tv_common(int channel)
{
    int ret = 0;
    int tv_value = 0;
    int param_id = 0;

    pr_info("TI-SmartPA: %s, channel=%d", __func__, channel);

    if (s_tas_smartamp_enable && (s_calib_test_flag || s_debug_enable)) {
        param_id = TAS_CALC_PARAM_IDX(TAS_SA_GET_TV, 1, channel);
        ret = tas25xx_smartamp_algo_ctrl((u8 *)&tv_value, param_id,
            TAS_GET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
        if (ret < 0) {
            pr_err("TI-SmartPA: %s: Failed to get Tv\n", __func__);
        }
        else {
            ret = tv_value;
        }
    }

    return ret;
}

static int tas25xx_get_tv_left(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{

    int ret = tas25xx_get_tv_common(CHANNEL0);
    if (ret >= 0) {
        pUcontrol->value.integer.value[0] = ret;
        pr_info("TI-SmartPA: %s: Getting Tv %d\n", __func__, ret);
        ret = 0;
    }

    return ret;
}

/*Control-8: Smartamp Enable*/
static const char *tas25xx_smartamp_enable_text[] = {
    "DISABLE",
    "ENABLE"
};

static const struct soc_enum tas25xx_smartamp_enable_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tas25xx_smartamp_enable_text), tas25xx_smartamp_enable_text),
};

static int tas25xx_smartamp_enable_set(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = 0;
    int param_id = 0;
    int user_data = pUcontrol->value.integer.value[0];
#ifdef CONFIG_SET_RE_IN_KERNEL
    uint32_t calibration_data[3];
#endif

#ifdef CONFIG_TAS25XX_ALGO_STEREO
    int number_of_ch = 2;
#else
    int number_of_ch = 1;
#endif

    if (s_tas_smartamp_bypass) {
        pr_info("TI-SmartPA: bypass enabled, not enabling\n");
        return 0;
    }

    pr_info("TI-SmartPA: %s: case %d, number_of_ch=%d\n", __func__, user_data, number_of_ch);

    s_tas_smartamp_enable = user_data;
    if (s_tas_smartamp_enable == 0) {
        s_profile_id = 0;
        s_calib_test_flag = 0;
        pr_info("TI-SmartPA: %s: Disable called\n", __func__);
        return 0;
    }

    pr_info("TI-SmartPA: %s: Setting the feedback module info for TAS\n", __func__);
    ret = afe_spk_prot_feed_back_cfg(TAS_TX_PORT, TAS_RX_PORT, 1, 0, 1);
    if (ret) {
        pr_err("TI-SmartPA: %s: FB Path Info failed ignoring ret = 0x%x\n", __func__, ret);
    }

    pr_info("TI-SmartPA: %s: Sending TX Enable\n", __func__);
    param_id = CAPI_V2_TAS_TX_ENABLE;
    ret = tas25xx_smartamp_algo_ctrl((u8 *)&user_data, param_id,
        TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_TX);
    if (ret) {
        pr_err("TI-SmartPA: %s: TX Enable Failed ret = 0x%x\n", __func__, ret);
        goto fail_cmd;
    }

    user_data = 0xB1B1B1B1;
    pr_info("TI-SmartPA: %s: Sending TX Config\n", __func__);
    param_id = CAPI_V2_TAS_TX_CFG;
    ret = tas25xx_smartamp_algo_ctrl((u8 *)&user_data, param_id,
        TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_TX);
    if (ret < 0) {
        pr_err("TI-SmartPA: %s: Failed to set config\n", __func__);
    }

    user_data = 1;
    pr_info("TI-SmartPA: %s: Sending RX Enable\n", __func__);
    param_id = CAPI_V2_TAS_RX_ENABLE;
    ret = tas25xx_smartamp_algo_ctrl((u8 *)&user_data, param_id,
        TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
    if (ret) {
        pr_err("TI-SmartPA: %s: RX Enable Failed ret = 0x%x\n", __func__, ret);
        goto fail_cmd;
    }

    user_data = 0xB1B1B1B1;
    pr_info("TI-SmartPA: %s: Sending RX Config\n", __func__);
    param_id = CAPI_V2_TAS_RX_CFG;
    ret = tas25xx_smartamp_algo_ctrl((u8 *)&user_data, param_id,
        TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
    if (ret < 0) {
        pr_err("TI-SmartPA: %s: Failed to set config\n", __func__);
    }

    s_tas_smartamp_enable = true;

#ifdef CONFIG_SET_RE_IN_KERNEL
    if (number_of_ch == 2) {
        ret = get_calibrated_re_tcalib(calibration_data, &calibration_data[2], number_of_ch);
    }
    else {
        ret = get_calibrated_re_tcalib(calibration_data, &calibration_data[1], number_of_ch);
    }

    if (ret) {
        pr_err("[Smartamp:%s] unable to get the calibration data = 0x%x\n", __func__, ret);
        //TODO: Ignore the calibration read error
        ret = 0;
    }
    else {
        int32_t t_cal;
        if (number_of_ch == 2) {
            t_cal = calibration_data[2];
            pr_info("[Smartamp:%s] setting re %d,%d and tcal %d\n", __func__
                , calibration_data[0], calibration_data[1], calibration_data[2]);
        }
        else {
            t_cal = calibration_data[1];
            pr_info("[Smartamp:%s] setting re %d and tcal %d\n", __func__
                , calibration_data[0], calibration_data[1]);
        }

        param_id = TAS_CALC_PARAM_IDX(TAS_SA_SET_TCAL, 1, CHANNEL0);
        ret = tas25xx_smartamp_algo_ctrl((u8 *)&t_cal,
            param_id, TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
        if (ret < 0) {
            pr_err("[Smartamp:%s] Failed to set Tcal\n", __func__);
            goto fail_cmd;
        }

        param_id = TAS_CALC_PARAM_IDX(TAS_SA_SET_RE, 1, CHANNEL0);
        ret = tas25xx_smartamp_algo_ctrl((u8 *)(&(calibration_data[0])),
            param_id, TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
        if (ret < 0) {
            pr_err("TI-SmartPA: %s: Failed to set Re\n", __func__);
            goto fail_cmd;
        }

        if (number_of_ch == 2) {
            param_id = TAS_CALC_PARAM_IDX(TAS_SA_SET_RE, 1, CHANNEL1);
            ret = tas25xx_smartamp_algo_ctrl((u8 *)&(calibration_data[1]),
                param_id, TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
            if (ret < 0) {
                pr_err("TI-SmartPA: %s: Failed to set Re\n", __func__);
                goto fail_cmd;
            }
        }
    }
#endif /* CONFIG_SET_RE_IN_KERNEL */

fail_cmd:
    return ret;
}

static int tas25xx_smartamp_enable_get(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = 0;
    int user_data = s_tas_smartamp_enable;
    pUcontrol->value.integer.value[0] = user_data;
    pr_info("TI-SmartPA: %s: case %d(0=DISABLE, 1=ENABLE)\n", __func__, user_data);
    return ret;
}

/*Control-9: Smartamp Bypass */
static const char *tas25xx_smartamp_bypass_text[] = {
    "FALSE",
    "TRUE"
};

static const struct soc_enum tas25xx_smartamp_bypass_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tas25xx_smartamp_bypass_text), tas25xx_smartamp_bypass_text),
};

static int tas25xx_smartamp_bypass_set(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = 0;
    int user_data = pUcontrol->value.integer.value[0];
    if (s_tas_smartamp_enable) {
        pr_debug("TI-SmartPA: %s: cannot update while smartamp enabled\n", __func__);
        return -EINVAL;
    }

    s_tas_smartamp_bypass = user_data;
    pr_info("TI-SmartPA: %s: case %d(FALSE=0,TRUE=1)\n", __func__, user_data);
    return ret;
}

static int tas25xx_smartamp_bypass_get(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = 0;
    pUcontrol->value.integer.value[0] = s_tas_smartamp_bypass;
    pr_info("TI-SmartPA: %s: case %d\n", __func__, s_tas_smartamp_bypass);
    return ret;
}

/*Control-9: Smartamp Bypass */
static const char *tas25xx_smartamp_debug_enable_text[] = {
    "FALSE",
    "TRUE"
};

static const struct soc_enum tas25xx_smartamp_debug_enable_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tas25xx_smartamp_debug_enable_text), tas25xx_smartamp_debug_enable_text),
};

static int tas25xx_smartamp_debug_enable_set(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = 0;
    int user_data = pUcontrol->value.integer.value[0];

    s_debug_enable = user_data;
    pr_info("TI-SmartPA: %s: case %d(FALSE=0,TRUE=1)\n", __func__, user_data);
    return ret;
}

static int tas25xx_smartamp_debug_enable_get(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = 0;
    pUcontrol->value.integer.value[0] = s_debug_enable;
    pr_info("TI-SmartPA: %s: case %d\n", __func__, s_debug_enable);
    return ret;
}

static int tas25xx_set_spk_id(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = 0;
    int speaker_id = pUcontrol->value.integer.value[0];
    int param_id = 0;

    pr_info("TI-SmartPA: %s: spk-id set %d\n", __func__, speaker_id);
    param_id = TAS_CALC_PARAM_IDX(TAS_SA_SET_SPKID, 1, CHANNEL0);
    ret = tas25xx_smartamp_algo_ctrl((u8 *)&speaker_id, param_id,
        TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
    if (ret < 0) {
        pr_err("TI-SmartPA: %s: Failed to set spk id\n", __func__);
    }

    return ret;
}

static int tas25xx_set_t_calib(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = 0;
    int calib_temp = pUcontrol->value.integer.value[0];
    int param_id = 0;

    pr_info("TI-SmartPA: %s: tcalib set %d\n", __func__, calib_temp);
    if (s_tas_smartamp_enable) {
        param_id = TAS_CALC_PARAM_IDX(TAS_SA_SET_TCAL, 1, CHANNEL0);
        ret = tas25xx_smartamp_algo_ctrl((u8 *)&calib_temp, param_id,
            TAS_SET_PARAM, sizeof(uint32_t), AFE_SMARTAMP_MODULE_RX);
        if (ret < 0) {
            pr_err("TI-SmartPA: %s: Failed to set spk id\n", __func__);
        }
    }

    return ret;
}

#ifdef CONFIG_TAS25XX_ALGO_STEREO
static int tas25xx_set_Re_right(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int re_value = pUcontrol->value.integer.value[0];

    pr_info("TI-SmartPA: %s: Setting Re %d\n", __func__, re_value);
    return tas25xx_set_Re_common(re_value, CHANNEL1);
}

static int tas25xx_get_re_right(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret;

    ret = tas25xx_get_re_common(CHANNEL1);
    if (ret >= 0) {
        pUcontrol->value.integer.value[0] = ret;
        ret = 0;
        pr_info("TI-SmartPA: %s: Getting Re value=%d\n", __func__, ret);
    }

    return ret;
}

static int tas25xx_get_f0_right(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = tas25xx_get_f0_common(CHANNEL1);

    if (ret >= 0) {
        pUcontrol->value.integer.value[0] = ret;
        pr_info("TI-SmartPA: %s: Getting F0 valu=%d\n", __func__, ret);
        ret = 0;
    }

    return ret;
}

static int tas25xx_get_q_right(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{
    int ret = tas25xx_get_q_common(CHANNEL1);
    if (ret >= 0) {
        pUcontrol->value.integer.value[0] = ret;
        pr_info("TI-SmartPA: %s: Getting Q val=%d\n", __func__, ret);
        ret = 0;
    }

    return ret;
}

static int tas25xx_get_tv_right(struct snd_kcontrol *pKcontrol,
    struct snd_ctl_elem_value *pUcontrol)
{

    int ret = tas25xx_get_tv_common(CHANNEL1);
    if (ret >= 0) {
        pUcontrol->value.integer.value[0] = ret;
        pr_info("TI-SmartPA: %s: Getting Tv %d\n", __func__, ret);
        ret = 0;
    }

    return ret;
}
#endif /* CONFIG_TAS25XX_ALGO_STEREO */

static const struct snd_kcontrol_new smartamp_tas25xx_mixer_controls[] = {
    SOC_ENUM_EXT("TAS25XX_DEBUG_ENABLE", tas25xx_smartamp_debug_enable_enum[0],
        tas25xx_smartamp_debug_enable_get, tas25xx_smartamp_debug_enable_set),
    SOC_ENUM_EXT("TAS25XX_ALGO_PROFILE", profile_index_enum[0],
        tas25xx_get_profile, tas25xx_set_profile),
    SOC_ENUM_EXT("TAS25XX_ALGO_CALIB_TEST", tas25xx_calib_test_enum[0],
        tas25xx_dummy_get, tas25xx_calib_test_set),
    SOC_SINGLE_EXT("TAS25XX_SET_SPK_ID", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_dummy_get, tas25xx_set_spk_id),
    SOC_SINGLE_EXT("TAS25XX_SET_T_CALIB", SND_SOC_NOPM, 0, 100, 0,
        tas25xx_dummy_get, tas25xx_set_t_calib),

    //left
    SOC_SINGLE_EXT("TAS25XX_SET_RE_LEFT", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_dummy_get, tas25xx_set_Re_left),
    SOC_SINGLE_EXT("TAS25XX_GET_RE_LEFT", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_get_re_left, tas25xx_dummy_set),
    SOC_SINGLE_EXT("TAS25XX_GET_F0_LEFT", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_get_f0_left, tas25xx_dummy_set),
    SOC_SINGLE_EXT("TAS25XX_GET_Q_LEFT", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_get_q_left, tas25xx_dummy_set),
    SOC_SINGLE_EXT("TAS25XX_GET_TV_LEFT", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_get_tv_left, tas25xx_dummy_set),

    //Right
#ifdef CONFIG_TAS25XX_ALGO_STEREO
    SOC_SINGLE_EXT("TAS25XX_SET_RE_RIGHT", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_dummy_get, tas25xx_set_Re_right),
    SOC_SINGLE_EXT("TAS25XX_GET_RE_RIGHT", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_get_re_right, tas25xx_dummy_set),
    SOC_SINGLE_EXT("TAS25XX_GET_F0_RIGHT", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_get_f0_right, tas25xx_dummy_set),
    SOC_SINGLE_EXT("TAS25XX_GET_Q_RIGHT", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_get_q_right, tas25xx_dummy_set),
    SOC_SINGLE_EXT("TAS25XX_GET_TV_RIGHT", SND_SOC_NOPM, 0, 0x7fffffff, 0,
        tas25xx_get_tv_right, tas25xx_dummy_set),
#endif /* CONFIG_TAS25XX_ALGO_STEREO */
    SOC_ENUM_EXT("TAS25XX_SMARTPA_ENABLE", tas25xx_smartamp_enable_enum[0],
        tas25xx_smartamp_enable_get, tas25xx_smartamp_enable_set),

    SOC_ENUM_EXT("TAS25XX_ALGO_BYPASS", tas25xx_smartamp_bypass_enum[0],
        tas25xx_smartamp_bypass_get, tas25xx_smartamp_bypass_set),
};

#if CODEC_CONTROL
void tas_smartamp_add_algo_controls(struct snd_soc_codec *codec)
{
    pr_err("TI-SmartPA: %s: Adding smartamp controls\n", __func__);
    mutex_init(&routing_lock);
    snd_soc_add_codec_controls(codec, smartamp_tas25xx_mixer_controls,
        ARRAY_SIZE(smartamp_tas25xx_mixer_controls));
    tas_calib_init();
}
EXPORT_SYMBOL(tas_smartamp_add_algo_controls);
#else
void tas_smartamp_add_algo_controls_for_platform(struct snd_soc_platform *platform)
{
    pr_err("TI-SmartPA: %s: Adding smartamp controls\n", __func__);
    mutex_init(&routing_lock);
    snd_soc_add_platform_controls(platform, smartamp_tas25xx_mixer_controls, ARRAY_SIZE(smartamp_tas25xx_mixer_controls));
    tas_calib_init();
}
EXPORT_SYMBOL(tas_smartamp_add_algo_controls_for_platform);
#endif /* CODEC_CONTROL */

void tas_smartamp_remove_algo_controls(struct snd_soc_codec *codec)
{
    (void)codec;
    tas_calib_exit();
}
EXPORT_SYMBOL(tas_smartamp_remove_algo_controls);
