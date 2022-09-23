/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT) */
/*
 * Allwinner A31 ISP Configuration
 */

#ifndef _UAPI_SUN6I_ISP_CONFIG_H
#define _UAPI_SUN6I_ISP_CONFIG_H

/* Allwinner A31 ISP meta-data formats */
#define V4L2_META_FMT_SUN6I_ISP_PARAMS v4l2_fourcc('S', '6', 'I', 'P') /* Allwinner A31 ISP Parameters */

#include <linux/types.h>

#define SUN6I_ISP_MODULE_BAYER			(1U << 0)
#define SUN6I_ISP_MODULE_BDNF			(1U << 1)

struct sun6i_isp_params_config_bayer {
	__u16	offset_r;
	__u16	offset_gr;
	__u16	offset_gb;
	__u16	offset_b;

	__u16	gain_r;
	__u16	gain_gr;
	__u16	gain_gb;
	__u16	gain_b;
};

struct sun6i_isp_params_config_bdnf {
	__u8	in_dis_min; // 8
	__u8	in_dis_max; // 10

	__u8	coefficients_g[7];
	__u8	coefficients_rb[5];
};

struct sun6i_isp_params_config {
	__u32					modules_used;

	struct sun6i_isp_params_config_bayer	bayer;
	struct sun6i_isp_params_config_bdnf	bdnf;
};

#endif /* _UAPI_SUN6I_ISP_CONFIG_H */
