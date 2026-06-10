// SPDX-License-Identifier: GPL-2.0
/*
 * DSM DS124 modules expect this Realtek platform export.  On RK3399,
 * bridge it to the generic thermal framework instead of enabling RTD LSP.
 */

#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/thermal.h>

int syno_rtd_get_temperature(void)
{
	static const char * const zone_names[] = {
		"cpu",
		"cpu_thermal",
		"soc-thermal",
		"soc_thermal",
		"gpu",
	};
	struct thermal_zone_device *tz;
	int temp;
	int i;

	for (i = 0; i < ARRAY_SIZE(zone_names); i++) {
		tz = thermal_zone_get_zone_by_name(zone_names[i]);
		if (IS_ERR(tz))
			continue;

		if (!thermal_zone_get_temp(tz, &temp))
			return temp / 1000;
	}

	return -1;
}
EXPORT_SYMBOL(syno_rtd_get_temperature);
