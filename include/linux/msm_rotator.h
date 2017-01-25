#ifndef __MSM_ROTATOR_H__
#define __MSM_ROTATOR_H__

#include <uapi/linux/msm_rotator.h>

struct msm_rotator_platform_data {
	unsigned int number_of_clocks;
	unsigned int hardware_version_number;
	struct msm_rot_clocks *rotator_clks;
#ifdef CONFIG_MSM_BUS_SCALING
	struct msm_bus_scale_pdata *bus_scale_table;
#endif
};
#endif

