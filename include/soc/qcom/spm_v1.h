/* Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __SOC_QCOM_SPM_V1_H__
#define __SOC_QCOM_SPM_V1_H__

enum {
	MSM_SPM_MODE_DISABLED,
	MSM_SPM_MODE_CLOCK_GATING,
	MSM_SPM_MODE_POWER_RETENTION,
	MSM_SPM_MODE_POWER_COLLAPSE,
	MSM_SPM_MODE_NR
};

enum {
	MSM_SPM_L2_MODE_DISABLED = MSM_SPM_MODE_DISABLED,
	MSM_SPM_L2_MODE_RETENTION,
	MSM_SPM_L2_MODE_GDHS,
	MSM_SPM_L2_MODE_POWER_COLLAPSE,
};

#if defined(CONFIG_MSM_SPM_V1)
/**
 * msm_spm_set_low_power_mode() - Configure SPM start address for low power mode
 * @mode: SPM LPM mode to enter
 * @notify_rpm: Notify RPM in this mode
 */
int msm_spm_set_low_power_mode(unsigned int mode, bool notify_rpm);

/* Internal low power management specific functions */

/**
 * msm_spm_reinit(): Reinitialize SPM registers
 */
void msm_spm_reinit(void);

#else /* defined(CONFIG_MSM_SPM_V1) */

static inline int msm_spm_set_low_power_mode(unsigned int mode, bool notify_rpm)
{
	return -ENOSYS;
}

static inline void msm_spm_reinit(void)
{
	/* empty */
}

#endif /*defined(CONFIG_MSM_SPM_V1) */

#endif /* __SOC_QCOM_SPM_V1_H__ */
