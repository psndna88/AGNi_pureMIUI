// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/of_fdt.h>
#include "msm_vidc_internal.h"
#include "msm_vidc_debug.h"


#define DDR_TYPE_LPDDR4 0x6
#define DDR_TYPE_LPDDR4X 0x7
#define DDR_TYPE_LPDDR4Y 0x8
#define DDR_TYPE_LPDDR5 0x9

#define CODEC_ENTRY(n, p, vsp, vpp, lp) \
{	\
	.fourcc = n,		\
	.session_type = p,	\
	.vsp_cycles = vsp,	\
	.vpp_cycles = vpp,	\
	.low_power_cycles = lp	\
}

#define EFUSE_ENTRY(sa, s, m, sh, p) \
{	\
	.start_address = sa,		\
	.size = s,	\
	.mask = m,	\
	.shift = sh,	\
	.purpose = p	\
}

#define UBWC_CONFIG(mco, mlo, hbo, bslo, bso, rs, mc, ml, hbb, bsl, bsp) \
{	\
	.override_bit_info.max_channel_override = mco,	\
	.override_bit_info.mal_length_override = mlo,	\
	.override_bit_info.hb_override = hbo,	\
	.override_bit_info.bank_swzl_level_override = bslo,	\
	.override_bit_info.bank_spreading_override = bso,	\
	.override_bit_info.reserved = rs,	\
	.max_channels = mc,	\
	.mal_length = ml,	\
	.highest_bank_bit = hbb,	\
	.bank_swzl_level = bsl,	\
	.bank_spreading = bsp,	\
}

static struct msm_vidc_codec_data default_codec_data[] =  {
	CODEC_ENTRY(V4L2_PIX_FMT_H264, MSM_VIDC_ENCODER, 125, 675, 320),
	CODEC_ENTRY(V4L2_PIX_FMT_H264, MSM_VIDC_DECODER, 125, 675, 320),
};

/* Update with Lahaina data */
static struct msm_vidc_codec_data lahaina_codec_data[] =  {
	CODEC_ENTRY(V4L2_PIX_FMT_H264, MSM_VIDC_ENCODER, 25, 675, 320),
	CODEC_ENTRY(V4L2_PIX_FMT_HEVC, MSM_VIDC_ENCODER, 25, 675, 320),
	CODEC_ENTRY(V4L2_PIX_FMT_VP8, MSM_VIDC_ENCODER, 25, 675, 320),
	CODEC_ENTRY(V4L2_PIX_FMT_MPEG2, MSM_VIDC_DECODER, 25, 200, 200),
	CODEC_ENTRY(V4L2_PIX_FMT_H264, MSM_VIDC_DECODER, 25, 200, 200),
	CODEC_ENTRY(V4L2_PIX_FMT_HEVC, MSM_VIDC_DECODER, 25, 200, 200),
	CODEC_ENTRY(V4L2_PIX_FMT_VP8, MSM_VIDC_DECODER, 25, 200, 200),
	CODEC_ENTRY(V4L2_PIX_FMT_VP9, MSM_VIDC_DECODER, 25, 200, 200),
};

static struct msm_vidc_codec_data bengal_codec_data[] =  {
	CODEC_ENTRY(V4L2_PIX_FMT_H264, MSM_VIDC_ENCODER, 125, 675, 320),
	CODEC_ENTRY(V4L2_PIX_FMT_HEVC, MSM_VIDC_ENCODER, 125, 675, 320),
	CODEC_ENTRY(V4L2_PIX_FMT_VP8, MSM_VIDC_ENCODER, 125, 675, 320),
	CODEC_ENTRY(V4L2_PIX_FMT_MPEG2, MSM_VIDC_DECODER, 50, 200, 200),
	CODEC_ENTRY(V4L2_PIX_FMT_H264, MSM_VIDC_DECODER, 50, 200, 200),
	CODEC_ENTRY(V4L2_PIX_FMT_HEVC, MSM_VIDC_DECODER, 50, 200, 200),
	CODEC_ENTRY(V4L2_PIX_FMT_VP8, MSM_VIDC_DECODER, 50, 200, 200),
	CODEC_ENTRY(V4L2_PIX_FMT_VP9, MSM_VIDC_DECODER, 50, 200, 200),
};

#define ENC     HAL_VIDEO_DOMAIN_ENCODER
#define DEC     HAL_VIDEO_DOMAIN_DECODER
#define H264    HAL_VIDEO_CODEC_H264
#define HEVC    HAL_VIDEO_CODEC_HEVC
#define VP8     HAL_VIDEO_CODEC_VP8
#define VP9     HAL_VIDEO_CODEC_VP9
#define MPEG2   HAL_VIDEO_CODEC_MPEG2
#define DOMAINS_ALL    (HAL_VIDEO_DOMAIN_ENCODER | HAL_VIDEO_DOMAIN_DECODER)
#define CODECS_ALL     (HAL_VIDEO_CODEC_H264 | HAL_VIDEO_CODEC_HEVC | \
			HAL_VIDEO_CODEC_VP8 | HAL_VIDEO_CODEC_VP9 | \
			HAL_VIDEO_CODEC_MPEG2)

static struct msm_vidc_codec bengal_codecs[] = {
	/* {domain, codec} */
	{DEC, H264}, {DEC, HEVC}, {DEC, VP9},
	{ENC, H264}, {ENC, HEVC},
};

static struct msm_vidc_codec default_codecs[] = {
	/* {domain, codec} */
	{DEC, H264}, {DEC, HEVC}, {DEC, VP8}, {DEC, VP9}, {DEC, MPEG2},
	{ENC, H264}, {ENC, HEVC}, {ENC, VP8},
};

static struct msm_vidc_codec_capability bengal_capabilities[] = {
	/* {cap_type, domains, codecs, min, max, step_size, default_value} */
	{CAP_FRAME_WIDTH, DOMAINS_ALL, CODECS_ALL, 96, 1920, 1, 1920},
	{CAP_FRAME_HEIGHT, DOMAINS_ALL, CODECS_ALL, 96, 1920, 1, 1080},
	/*  ((1920 * 1080) / 256) */
	{CAP_MBS_PER_FRAME, DOMAINS_ALL, CODECS_ALL, 36, 8160, 1, 34560},
	/* 1080@30 decode + 1080@30 encode */
	{CAP_MBS_PER_SECOND, DOMAINS_ALL, CODECS_ALL, 36, 486000, 1, 243000},
	{CAP_FRAMERATE, DOMAINS_ALL, CODECS_ALL, 1, 120, 1, 30},
	{CAP_BITRATE, DOMAINS_ALL, CODECS_ALL, 1, 40000000, 1, 20000000},
	{CAP_BFRAME, ENC, H264|HEVC, 0, 1, 1, 0},
	{CAP_HIER_P_NUM_ENH_LAYERS, ENC, H264|HEVC, 0, 6, 1, 0},
	{CAP_LTR_COUNT, ENC, H264|HEVC, 0, 4, 1, 0},
	/* ((1920 * 1088) / 256) * 30 fps */
	{CAP_MBS_PER_SECOND_POWER_SAVE, ENC, CODECS_ALL,
		0, 244800, 1, 244800},
	{CAP_I_FRAME_QP, ENC, H264|HEVC, 0, 51, 1, 10},
	{CAP_P_FRAME_QP, ENC, H264|HEVC, 0, 51, 1, 20},
	{CAP_B_FRAME_QP, ENC, H264|HEVC, 0, 51, 1, 20},

	/* 10 slices */
	{CAP_SLICE_BYTE, ENC, H264|HEVC, 1, 10, 1, 10},
	{CAP_SLICE_MB, ENC, H264|HEVC, 1, 10, 1, 10},
	{CAP_MAX_VIDEOCORES, DOMAINS_ALL, CODECS_ALL, 0, 1, 1, 1},

	/* Secure usecase specific */
	{CAP_SECURE_FRAME_WIDTH, DOMAINS_ALL, CODECS_ALL, 96, 1920, 1, 1920},
	{CAP_SECURE_FRAME_HEIGHT, DOMAINS_ALL, CODECS_ALL, 96, 1920, 1, 1080},
	/* (1920 * 1088) / 256 */
	{CAP_SECURE_MBS_PER_FRAME, DOMAINS_ALL, CODECS_ALL, 36, 8160, 1, 34560},
	{CAP_SECURE_BITRATE, DOMAINS_ALL, CODECS_ALL, 1, 40000000, 1, 20000000},
};

static struct msm_vidc_codec_capability lahaina_capabilities[] = {
	/* {cap_type, domains, codecs, min, max, step_size, default_value,} */
	{CAP_FRAME_WIDTH, DOMAINS_ALL, CODECS_ALL, 128, 8192, 1, 1920},
	{CAP_FRAME_HEIGHT, DOMAINS_ALL, CODECS_ALL, 128, 8192, 1, 1080},
	/* (8192 * 4320) / 256 */
	{CAP_MBS_PER_FRAME, DOMAINS_ALL, CODECS_ALL, 64, 138240, 1, 138240},
	/* ((1920 * 1088) / 256) * 960 fps */
	{CAP_MBS_PER_SECOND, DOMAINS_ALL, CODECS_ALL, 64, 7833600, 1, 7833600},
	{CAP_FRAMERATE, DOMAINS_ALL, CODECS_ALL, 1, 960, 1, 30},
	{CAP_BITRATE, DOMAINS_ALL, CODECS_ALL, 1, 220000000, 1, 20000000},
	{CAP_SCALE_X, ENC, CODECS_ALL, 8192, 65536, 1, 8192},
	{CAP_SCALE_Y, ENC, CODECS_ALL, 8192, 65536, 1, 8192},
	{CAP_SCALE_X, DEC, CODECS_ALL, 65536, 65536, 1, 65536},
	{CAP_SCALE_Y, DEC, CODECS_ALL, 65536, 65536, 1, 65536},
	{CAP_BFRAME, ENC, H264|HEVC, 0, 1, 1, 0},
	{CAP_HIER_P_NUM_ENH_LAYERS, ENC, H264|HEVC, 0, 6, 1, 0},
	{CAP_LTR_COUNT, ENC, H264|HEVC, 0, 2, 1, 0},
	/* ((4096 * 2304) / 256) * 60 fps */
	{CAP_MBS_PER_SECOND_POWER_SAVE, ENC, CODECS_ALL,
		0, 2211840, 1, 2211840},
	{CAP_I_FRAME_QP, ENC, H264|HEVC, 0, 51, 1, 10},
	{CAP_P_FRAME_QP, ENC, H264|HEVC, 0, 51, 1, 20},
	{CAP_B_FRAME_QP, ENC, H264|HEVC, 0, 51, 1, 20},
	{CAP_I_FRAME_QP, ENC, VP8|VP9, 0, 127, 1, 20},
	{CAP_P_FRAME_QP, ENC, VP8|VP9, 0, 127, 1, 40},
	{CAP_B_FRAME_QP, ENC, VP8|VP9, 0, 127, 1, 40},
	/* 10 slices */
	{CAP_SLICE_BYTE, ENC, H264|HEVC, 1, 10, 1, 10},
	{CAP_SLICE_MB, ENC, H264|HEVC, 1, 10, 1, 10},
	{CAP_MAX_VIDEOCORES, DOMAINS_ALL, CODECS_ALL, 0, 1, 1, 1},

	/* VP8 specific */
	{CAP_FRAME_WIDTH, ENC|DEC, VP8, 128, 4096, 1, 1920},
	{CAP_FRAME_HEIGHT, ENC|DEC, VP8, 128, 4096, 1, 1080},
	/* (4096 * 2304) / 256 */
	{CAP_MBS_PER_FRAME, ENC|DEC, VP8, 64, 36864, 1, 8160},
	/* ((4096 * 2304) / 256) * 120 */
	{CAP_MBS_PER_SECOND, ENC|DEC, VP8, 64, 4423680, 1, 244800},
	{CAP_BFRAME, ENC, VP8, 0, 0, 1, 0},
	{CAP_FRAMERATE, ENC, VP8, 1, 60, 1, 30},
	{CAP_FRAMERATE, DEC, VP8, 1, 120, 1, 30},
	{CAP_BITRATE, ENC, VP8, 1, 74000000, 1, 20000000},
	{CAP_BITRATE, DEC, VP8, 1, 220000000, 1, 20000000},

	/* Mpeg2 decoder specific */
	{CAP_FRAME_WIDTH, DEC, MPEG2, 128, 1920, 1, 1920},
	{CAP_FRAME_HEIGHT, DEC, MPEG2, 128, 1920, 1, 1080},
	/* (1920 * 1088) / 256 */
	{CAP_MBS_PER_FRAME, DEC, MPEG2, 64, 8160, 1, 8160},
	/* ((1920 * 1088) / 256) * 30*/
	{CAP_MBS_PER_SECOND, DEC, MPEG2, 64, 244800, 1, 244800},
	{CAP_FRAMERATE, DEC, MPEG2, 1, 30, 1, 30},
	{CAP_BITRATE, DEC, MPEG2, 1, 40000000, 1, 20000000},

	/* Secure usecase specific */
	{CAP_SECURE_FRAME_WIDTH, DOMAINS_ALL, CODECS_ALL, 128, 4096, 1, 1920},
	{CAP_SECURE_FRAME_HEIGHT, DOMAINS_ALL, CODECS_ALL, 128, 4096, 1, 1080},
	/* (4096 * 2304) / 256 */
	{CAP_SECURE_MBS_PER_FRAME, DOMAINS_ALL, CODECS_ALL, 64, 36864, 1, 36864},
	{CAP_SECURE_BITRATE, DOMAINS_ALL, CODECS_ALL, 1, 40000000, 1, 20000000},

	/* Batch Mode Decode */
	{CAP_BATCH_MAX_MB_PER_FRAME, DEC, CODECS_ALL, 64, 34560, 1, 34560},
	/* (4096 * 2160) / 256 */
	{CAP_BATCH_MAX_FPS, DEC, CODECS_ALL, 1, 120, 1, 120},

	/* Lossless encoding usecase specific */
	{CAP_LOSSLESS_FRAME_WIDTH, ENC, H264|HEVC, 128, 4096, 1, 1920},
	{CAP_LOSSLESS_FRAME_HEIGHT, ENC, H264|HEVC, 128, 4096, 1, 1080},
	/* (4096 * 2304) / 256 */
	{CAP_LOSSLESS_MBS_PER_FRAME, ENC, H264|HEVC, 64, 36864, 1, 36864},

	/* All intra encoding usecase specific */
	{CAP_ALLINTRA_MAX_FPS, ENC, H264|HEVC, 1, 240, 1, 30},

	/* Image specific */
	{CAP_HEVC_IMAGE_FRAME_WIDTH, ENC, HEVC, 128, 512, 1, 512},
	{CAP_HEVC_IMAGE_FRAME_HEIGHT, ENC, HEVC, 128, 512, 1, 512},
	{CAP_HEIC_IMAGE_FRAME_WIDTH, ENC, HEVC, 512, 16384, 1, 16384},
	{CAP_HEIC_IMAGE_FRAME_HEIGHT, ENC, HEVC, 512, 16384, 1, 16384},

	/* Level for AVC and HEVC encoder specific */
	{CAP_H264_LEVEL, ENC, H264, V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
	                            V4L2_MPEG_VIDEO_H264_LEVEL_6_0, 1,
	                            V4L2_MPEG_VIDEO_H264_LEVEL_6_0},
	{CAP_HEVC_LEVEL, ENC, HEVC, V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
	                            V4L2_MPEG_VIDEO_HEVC_LEVEL_6, 1,
	                            V4L2_MPEG_VIDEO_HEVC_LEVEL_6},

	/* Level for AVC, HEVC and VP9 decoder specific */
	{CAP_H264_LEVEL, DEC, H264, V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
	                            V4L2_MPEG_VIDEO_H264_LEVEL_6_1, 1,
	                            V4L2_MPEG_VIDEO_H264_LEVEL_5_0},
	{CAP_HEVC_LEVEL, DEC, HEVC, V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
	                            V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1, 1,
	                            V4L2_MPEG_VIDEO_HEVC_LEVEL_5},
};

/*
 * Custom conversion coefficients for resolution: 176x144 negative
 * coeffs are converted to s4.9 format
 * (e.g. -22 converted to ((1 << 13) - 22)
 * 3x3 transformation matrix coefficients in s4.9 fixed point format
 */
static u32 vpe_csc_custom_matrix_coeff[HAL_MAX_MATRIX_COEFFS] = {
	470, 8170, 8148, 0, 490, 50, 0, 34, 483
};

/* offset coefficients in s9 fixed point format */
static u32 vpe_csc_custom_bias_coeff[HAL_MAX_BIAS_COEFFS] = {
	34, 0, 4
};

/* clamping value for Y/U/V([min,max] for Y/U/V) */
static u32 vpe_csc_custom_limit_coeff[HAL_MAX_LIMIT_COEFFS] = {
	16, 235, 16, 240, 16, 240
};

static struct msm_vidc_common_data default_common_data[] = {
	{
		.key = "qcom,never-unload-fw",
		.value = 1,
	},
};

static struct msm_vidc_common_data lahaina_common_data[] = {
	{
		.key = "qcom,never-unload-fw",
		.value = 1,
	},
	{
		.key = "qcom,sw-power-collapse",
		.value = 1,
	},
	{
		.key = "qcom,domain-attr-non-fatal-faults",
		.value = 1,
	},
	{
		.key = "qcom,max-secure-instances",
		.value = 3,
	},
	{
		.key = "qcom,max-hw-load",
		.value = 7776000,       /*
					 * 7680x4320@60fps, 3840x2160@240fps
					 * Greater than 4096x2160@120fps,
					 *  8192x4320@48fps
					 */
	},
	{
		.key = "qcom,max-mbpf",
		.value = 172800,	/* (8192x4320)/256 + (4096x2160)/256*/
	},
	{
		.key = "qcom,max-hq-mbs-per-frame",
		.value = 34560,		/* 4096x2160 */
	},
	{
		.key = "qcom,max-hq-mbs-per-sec",
		.value = 1036800,	/* 4096x2160@30fps */
	},
	{
		.key = "qcom,max-b-frame-mbs-per-frame",
		.value = 32400, /* 3840x2160/256 */
	},
	{
		.key = "qcom,max-b-frame-mbs-per-sec",
		.value = 1944000, /* 3840x2160/256 MBs@60fps */
	},
	{
		.key = "qcom,power-collapse-delay",
		.value = 1500,
	},
	{
		.key = "qcom,hw-resp-timeout",
		.value = 1000,
	},
	{
		.key = "qcom,debug-timeout",
		.value = 0,
	},
	{
		.key = "qcom,decode-batching",
		.value = 1,
	},
	{
		.key = "qcom,batch-timeout",
		.value = 200,
	},
	{
		.key = "qcom,dcvs",
		.value = 1,
	},
	{
		.key = "qcom,fw-cycles",
		.value = 326389,
	},
	{
		.key = "qcom,fw-vpp-cycles",
		.value = 44156,
	},
	{
		.key = "qcom,avsync-window-size",
		.value = 40,
	},
};

static struct msm_vidc_common_data bengal_common_data[] = {
	{
		.key = "qcom,never-unload-fw",
		.value = 1,
	},
	{
		.key = "qcom,sw-power-collapse",
		.value = 1,
	},
	{
		.key = "qcom,domain-attr-non-fatal-faults",
		.value = 1,
	},
	{
		.key = "qcom,max-secure-instances",
		.value = 5,
	},
	{
		.key = "qcom,max-hw-load",
		.value = 1216800,
	},
	{
		.key = "qcom,max-hq-mbs-per-frame",
		.value = 8160,
	},
	{
		.key = "qcom,max-hq-mbs-per-sec",
		.value = 244800,  /* 1920 x 1088 @ 30 fps */
	},
	{
		.key = "qcom,max-b-frame-mbs-per-frame",
		.value = 8160,
	},
	{
		.key = "qcom,max-b-frame-mbs-per-sec",
		.value = 489600,
	},
	{
		.key = "qcom,power-collapse-delay",
		.value = 1500,
	},
	{
		.key = "qcom,hw-resp-timeout",
		.value = 1000,
	},
	{
		.key = "qcom,dcvs",
		.value = 1,
	},
	{
		.key = "qcom,fw-cycles",
		.value = 733003,
	},
	{
		.key = "qcom,fw-vpp-cycles",
		.value = 225975,
	},
};

/* Default UBWC config for LPDDR5 */
static struct msm_vidc_ubwc_config_data lahaina_ubwc_data[] = {
	UBWC_CONFIG(1, 1, 1, 0, 0, 0, 8, 32, 16, 0, 0),
};

static struct msm_vidc_platform_data default_data = {
	.codec_data = default_codec_data,
	.codec_data_length =  ARRAY_SIZE(default_codec_data),
	.common_data = default_common_data,
	.common_data_length =  ARRAY_SIZE(default_common_data),
	.csc_data.vpe_csc_custom_bias_coeff = vpe_csc_custom_bias_coeff,
	.csc_data.vpe_csc_custom_matrix_coeff = vpe_csc_custom_matrix_coeff,
	.csc_data.vpe_csc_custom_limit_coeff = vpe_csc_custom_limit_coeff,
	.efuse_data = NULL,
	.efuse_data_length = 0,
	.sku_version = 0,
	.vpu_ver = VPU_VERSION_IRIS2,
	.ubwc_config = 0x0,
};

static struct msm_vidc_platform_data lahaina_data = {
	.codec_data = lahaina_codec_data,
	.codec_data_length =  ARRAY_SIZE(lahaina_codec_data),
	.common_data = lahaina_common_data,
	.common_data_length =  ARRAY_SIZE(lahaina_common_data),
	.csc_data.vpe_csc_custom_bias_coeff = vpe_csc_custom_bias_coeff,
	.csc_data.vpe_csc_custom_matrix_coeff = vpe_csc_custom_matrix_coeff,
	.csc_data.vpe_csc_custom_limit_coeff = vpe_csc_custom_limit_coeff,
	.efuse_data = NULL,
	.efuse_data_length = 0,
	.sku_version = 0,
	.vpu_ver = VPU_VERSION_IRIS2,
	.ubwc_config = lahaina_ubwc_data,
	.codecs = default_codecs,
	.codecs_count = ARRAY_SIZE(default_codecs),
	.codec_caps = lahaina_capabilities,
	.codec_caps_count = ARRAY_SIZE(lahaina_capabilities),
};

static struct msm_vidc_platform_data bengal_data = {
	.codec_data = bengal_codec_data,
	.codec_data_length =  ARRAY_SIZE(bengal_codec_data),
	.common_data = bengal_common_data,
	.common_data_length =  ARRAY_SIZE(bengal_common_data),
	.csc_data.vpe_csc_custom_bias_coeff = vpe_csc_custom_bias_coeff,
	.csc_data.vpe_csc_custom_matrix_coeff = vpe_csc_custom_matrix_coeff,
	.csc_data.vpe_csc_custom_limit_coeff = vpe_csc_custom_limit_coeff,
	.efuse_data = NULL,
	.efuse_data_length = 0,
	.sku_version = 0,
	.vpu_ver = VPU_VERSION_AR50_LITE,
	.ubwc_config = 0x0,
	.codecs = bengal_codecs,
	.codecs_count = ARRAY_SIZE(bengal_codecs),
	.codec_caps = bengal_capabilities,
	.codec_caps_count = ARRAY_SIZE(bengal_capabilities),
};

static const struct of_device_id msm_vidc_dt_match[] = {
	{
		.compatible = "qcom,lahaina-vidc",
		.data = &lahaina_data,
	},
	{
		.compatible = "qcom,bengal-vidc",
		.data = &bengal_data,
	},
	{},
};

MODULE_DEVICE_TABLE(of, msm_vidc_dt_match);

void *vidc_get_drv_data(struct device *dev)
{
	struct msm_vidc_platform_data *driver_data = NULL;
	const struct of_device_id *match;
	uint32_t ddr_type = DDR_TYPE_LPDDR5;

	if (!IS_ENABLED(CONFIG_OF) || !dev->of_node) {
		driver_data = &default_data;
		goto exit;
	}

	match = of_match_node(msm_vidc_dt_match, dev->of_node);

	if (match)
		driver_data = (struct msm_vidc_platform_data *)match->data;

	if (!of_find_property(dev->of_node, "sku-index", NULL) ||
			!driver_data) {
		goto exit;
	} else if (!strcmp(match->compatible, "qcom,lahaina-vidc")) {
		ddr_type = of_fdt_get_ddrtype();
		if (ddr_type == -ENOENT) {
			d_vpr_e("Failed to get ddr type, use LPDDR5\n");
		}
		d_vpr_h("DDR Type %x\n", ddr_type);

		if (driver_data->ubwc_config &&
			(ddr_type == DDR_TYPE_LPDDR4 ||
			ddr_type == DDR_TYPE_LPDDR4X ||
			ddr_type == DDR_TYPE_LPDDR4Y))
			driver_data->ubwc_config->highest_bank_bit = 0xf;
	}
exit:
	return driver_data;
}
