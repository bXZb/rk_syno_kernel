// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal Synology synobios compatibility shim for RK3399 DSM boots.
 *
 * DSM userspace treats /dev/synobios open/ioctl failures as a hardware
 * identity fault on DS423.  RK3399 has no Realtek microcontroller, so this
 * module only provides the ABI surface DSM probes during boot and Web UI
 * startup.  It intentionally performs no hardware power, buzzer, fan or LED
 * action.
 */

#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/synobios.h>
#include <linux/thermal.h>
#include <linux/uaccess.h>

#define SYNOBIOS_NAME "synobios"
#define SYNOBIOS_MAJOR 201
#define SYNOBIOS_MAX_IOCTL_COPY 16384
#define SYNOBIOS_SERIAL_LEN 32
#define SYNOBIOS_LEGACY_IOC_MAGIC 'A'
#define SYNOBIOS_CPU_CLOCK_FALLBACK_MHZ 1000
#define SYNOBIOS_CPU_VENDOR_FALLBACK "ARM"
#define SYNOBIOS_CPU_FAMILY_FALLBACK "Cortex"
#define SYNOBIOS_CPU_SERIES_FALLBACK "SoC"

#ifndef SYNOIO_SERIAL
#define SYNOIO_SERIAL _IOR(SYNOBIOS_LEGACY_IOC_MAGIC, 105, int)
#endif
#ifndef SYNOIO_SYNOVER
#define SYNOIO_SYNOVER _IOR(SYNOBIOS_LEGACY_IOC_MAGIC, 107, unsigned long)
#endif
#ifndef SYNOIO_GETSERIALNUM
#define SYNOIO_GETSERIALNUM _IOR(SYNOBIOS_LEGACY_IOC_MAGIC, 108, off_t)
#endif
#ifndef SYNOIO_HWHDSUPPORT
#define SYNOIO_HWHDSUPPORT _IOR(SYNOBIOS_LEGACY_IOC_MAGIC, 110, int)
#endif
#ifndef SYNOIO_HWTHERMALSUPPORT
#define SYNOIO_HWTHERMALSUPPORT _IOR(SYNOBIOS_LEGACY_IOC_MAGIC, 111, int)
#endif

#ifndef SYNOIO_BUTTON_POWER
#define SYNOIO_BUTTON_POWER _IOWR(SYNOBIOS_IOC_MAGIC, 7, int)
#endif
#ifndef SYNOIO_BUTTON_RESET
#define SYNOIO_BUTTON_RESET _IOWR(SYNOBIOS_IOC_MAGIC, 8, int)
#endif
#ifndef SYNOIO_BUTTON_USB
#define SYNOIO_BUTTON_USB _IOWR(SYNOBIOS_IOC_MAGIC, 9, int)
#endif
#ifndef SYNOIO_GET_TEMPERATURE
#define SYNOIO_GET_TEMPERATURE _IOWR(SYNOBIOS_IOC_MAGIC, 16, int)
#endif
#ifndef SYNOIO_GET_HW_CAPABILITY
#define SYNOIO_GET_HW_CAPABILITY _IOWR(SYNOBIOS_IOC_MAGIC, 23, CAPABILITY)
#endif
#ifndef SYNOIO_GET_FAN_NUM
#define SYNOIO_GET_FAN_NUM _IOWR(SYNOBIOS_IOC_MAGIC, 24, int)
#endif
#ifndef SYNOIO_GET_CPU_TEMPERATURE
#define SYNOIO_GET_CPU_TEMPERATURE _IOWR(SYNOBIOS_IOC_MAGIC, 34, SYNOCPUTEMP)
#endif
#ifndef SYNOIO_CHECK_MICROP_ID
#define SYNOIO_CHECK_MICROP_ID _IO(SYNOBIOS_IOC_MAGIC, 40)
#endif
#ifndef SYNOIO_GET_COPY_BUTTON
#define SYNOIO_GET_COPY_BUTTON _IOWR(SYNOBIOS_IOC_MAGIC, 46, int)
#endif
#ifndef HWMON_GET_SUPPORT
#define HWMON_GET_SUPPORT _IOWR(SYNOBIOS_IOC_MAGIC, 301, SYNO_HWMON_SUPPORT)
#endif
#ifndef HWMON_GET_CPU_TEMPERATURE
#define HWMON_GET_CPU_TEMPERATURE _IOWR(SYNOBIOS_IOC_MAGIC, 302, SYNO_HWMON_SENSOR_TYPE)
#endif
#ifndef HWMON_GET_FAN_SPEED_RPM
#define HWMON_GET_FAN_SPEED_RPM _IOWR(SYNOBIOS_IOC_MAGIC, 303, SYNO_HWMON_SENSOR_TYPE)
#endif
#ifndef HWMON_GET_PSU_STATUS
#define HWMON_GET_PSU_STATUS _IOWR(SYNOBIOS_IOC_MAGIC, 304, SYNO_HWMON_SENSOR_TYPE)
#endif
#ifndef HWMON_GET_SYS_VOLTAGE
#define HWMON_GET_SYS_VOLTAGE _IOWR(SYNOBIOS_IOC_MAGIC, 305, SYNO_HWMON_SENSOR_TYPE)
#endif
#ifndef HWMON_GET_SYS_THERMAL
#define HWMON_GET_SYS_THERMAL _IOWR(SYNOBIOS_IOC_MAGIC, 306, SYNO_HWMON_SENSOR_TYPE)
#endif
#ifndef HWMON_GET_HDD_BACKPLANE
#define HWMON_GET_HDD_BACKPLANE _IOWR(SYNOBIOS_IOC_MAGIC, 307, SYNO_HWMON_SENSOR_TYPE)
#endif
#ifndef HWMON_GET_SYS_CURRENT
#define HWMON_GET_SYS_CURRENT _IOWR(SYNOBIOS_IOC_MAGIC, 308, SYNO_HWMON_SENSOR_TYPE)
#endif

static int check_fan;
static int system_mode;
static struct proc_dir_entry *proc_synobios_root;
static struct proc_dir_entry *proc_syno_cpu_arch;

extern char gszSerialNum[32];
extern char gszCustomSerialNum[32];
extern unsigned int gSynoCPUInfoCore;
extern char gSynoCPUInfoClock[16];

module_param(check_fan, int, 0644);
MODULE_PARM_DESC(check_fan, "accepted for DSM compatibility");
module_param(system_mode, int, 0644);
MODULE_PARM_DESC(system_mode, "accepted for DSM compatibility");

static int synobios_fake_event_handler(unsigned long long event, ...)
{
	return 0;
}

static int synobios_fake_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int synobios_fake_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int synobios_copy_zero_to_user(unsigned long arg, unsigned int size)
{
	void *buf;
	int ret = 0;

	if (!arg || !size)
		return 0;

	size = min_t(unsigned int, size, SYNOBIOS_MAX_IOCTL_COPY);
	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_to_user((void __user *)arg, buf, size))
		ret = -EFAULT;

	kfree(buf);
	return ret;
}

static int synobios_copy_to_user_value(unsigned long arg, const void *value,
				       unsigned int size)
{
	if (!arg || !size)
		return 0;

	if (copy_to_user((void __user *)arg, value, size))
		return -EFAULT;

	return 0;
}

static int synobios_copy_int_to_user(unsigned long arg, int value)
{
	return synobios_copy_to_user_value(arg, &value, sizeof(value));
}

static int synobios_copy_ulong_to_user(unsigned long arg, unsigned long value)
{
	return synobios_copy_to_user_value(arg, &value, sizeof(value));
}

static void synobios_get_serial(char *serial, size_t size)
{
	const char *src = NULL;

	if (!size)
		return;

	serial[0] = '\0';

	if (gszCustomSerialNum[0])
		src = gszCustomSerialNum;
	else if (gszSerialNum[0])
		src = gszSerialNum;

	if (src)
		strscpy(serial, src, size);
}

static int synobios_read_thermal_zone(const char *name, int fallback)
{
	struct thermal_zone_device *tz;
	int temp;

	tz = thermal_zone_get_zone_by_name(name);
	if (IS_ERR(tz))
		return fallback;

	if (thermal_zone_get_temp(tz, &temp))
		return fallback;

	return temp / 1000;
}

static int synobios_read_cpu_temp(void)
{
	return synobios_read_thermal_zone("cpu-thermal", 40);
}

static int synobios_read_sys_temp(void)
{
	return synobios_read_thermal_zone("gpu-thermal",
					  synobios_read_cpu_temp());
}

static bool synobios_has_pwm_fan(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "pwm-fan");
	if (!np)
		return false;

	of_node_put(np);
	return true;
}

static int synobios_read_fan_rpm(void)
{
	struct device_node *np;
	u32 max_rpm = 0;
	u32 level = 0;
	int count;

	np = of_find_compatible_node(NULL, NULL, "pwm-fan");
	if (!np)
		return 0;

	of_property_read_u32(np, "estimated-max-rpm", &max_rpm);
	count = of_property_count_u32_elems(np, "cooling-levels");
	if (count > 0)
		of_property_read_u32_index(np, "cooling-levels", count - 1,
					   &level);
	of_node_put(np);

	if (!max_rpm)
		max_rpm = 5000;
	if (!level)
		level = 255;

	return DIV_ROUND_CLOSEST(level * max_rpm, 255);
}

static void synobios_copy_capitalized(char *dst, size_t size, const char *src,
				      size_t len, bool upper)
{
	size_t i;

	if (!size)
		return;

	for (i = 0; i + 1 < size && i < len; i++) {
		if (upper)
			dst[i] = toupper(src[i]);
		else if (i == 0)
			dst[i] = toupper(src[i]);
		else
			dst[i] = src[i];
	}
	dst[i] = '\0';
}

static bool synobios_is_soc_compatible(const char *compatible)
{
	return !strncmp(compatible, "rockchip,", strlen("rockchip,"));
}

static bool synobios_get_root_compatible(const char **compatible)
{
	const char *first = NULL;
	const char *compat;
	int count;
	int i;

	if (!of_root)
		return false;

	count = of_property_count_strings(of_root, "compatible");
	if (count <= 0)
		return false;

	for (i = 0; i < count; i++) {
		if (of_property_read_string_index(of_root, "compatible", i,
						  &compat))
			continue;

		if (!first)
			first = compat;
		if (synobios_is_soc_compatible(compat)) {
			*compatible = compat;
			return true;
		}
	}

	if (!first)
		return false;

	*compatible = first;
	return true;
}

static bool synobios_get_compatible_cpu_arch(char *buf, size_t size)
{
	const char *compatible;
	const char *comma;
	char vendor[24];
	char family[24];

	if (!of_root || !size)
		return false;

	if (!synobios_get_root_compatible(&compatible))
		return false;

	comma = strchr(compatible, ',');
	if (!comma || comma == compatible || !comma[1])
		return false;

	synobios_copy_capitalized(vendor, sizeof(vendor), compatible,
				  comma - compatible, false);
	synobios_copy_capitalized(family, sizeof(family), comma + 1,
				  strlen(comma + 1), true);
	snprintf(buf, size, "%s, %s, %s", vendor, family,
		 SYNOBIOS_CPU_SERIES_FALLBACK);
	return true;
}

static unsigned int synobios_read_cpu_opp_max_mhz(void)
{
	struct device_node *opp_np;
	struct device_node *np;
	unsigned long long max_hz = 0;
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		struct device *dev = get_cpu_device(cpu);

		if (!dev || !dev->of_node)
			continue;

		opp_np = of_parse_phandle(dev->of_node, "operating-points-v2",
					  0);
		if (!opp_np)
			continue;

		for_each_available_child_of_node(opp_np, np) {
			u64 hz;

			if (!of_property_read_u64(np, "opp-hz", &hz) &&
			    hz > max_hz)
				max_hz = hz;
		}

		of_node_put(opp_np);
	}

	return DIV_ROUND_CLOSEST_ULL(max_hz, 1000000);
}

static unsigned int synobios_read_cpu_max_mhz(void)
{
	unsigned int cpu;
	unsigned int max_khz = 0;
	unsigned int max_mhz;

	for_each_possible_cpu(cpu) {
		unsigned int khz;

		khz = cpufreq_quick_get_max(cpu);
		if (!khz)
			khz = cpufreq_quick_get(cpu);
		if (khz > max_khz)
			max_khz = khz;
	}

	if (!max_khz)
		max_mhz = synobios_read_cpu_opp_max_mhz();
	else
		max_mhz = DIV_ROUND_CLOSEST(max_khz, 1000);

	return max_mhz ?: SYNOBIOS_CPU_CLOCK_FALLBACK_MHZ;
}

static void synobios_init_cpu_info(void)
{
	if (!gSynoCPUInfoCore)
		gSynoCPUInfoCore = num_possible_cpus();
	if (!gSynoCPUInfoClock[0])
		snprintf(gSynoCPUInfoClock, sizeof(gSynoCPUInfoClock), "%u",
			 synobios_read_cpu_max_mhz());
}

static void synobios_get_cpu_arch(char *buf, size_t size)
{
	if (!size)
		return;

	if (synobios_get_compatible_cpu_arch(buf, size))
		return;

	snprintf(buf, size, "%s, %s, %s",
		 SYNOBIOS_CPU_VENDOR_FALLBACK,
		 SYNOBIOS_CPU_FAMILY_FALLBACK,
		 SYNOBIOS_CPU_SERIES_FALLBACK);
}

static int synobios_copy_serial_to_user(unsigned long arg)
{
	char serial[SYNOBIOS_SERIAL_LEN];

	synobios_get_serial(serial, sizeof(serial));

	return synobios_copy_to_user_value(arg, serial,
					   strnlen(serial, sizeof(serial)) + 1);
}

static int synobios_get_hw_capability(unsigned long arg)
{
	CAPABILITY capability = {
		.id = CAPABILITY_NONE,
		.support = 0,
	};

	if (arg && copy_from_user(&capability, (void __user *)arg,
				  sizeof(capability)))
		return -EFAULT;

	switch (capability.id) {
	case CAPABILITY_THERMAL:
	case CAPABILITY_CPU_TEMP:
		capability.support = 1;
		break;
	case CAPABILITY_FAN_RPM_RPT:
		capability.support = synobios_has_pwm_fan();
		break;
	default:
		capability.support = 0;
		break;
	}

	return synobios_copy_to_user_value(arg, &capability,
					   sizeof(capability));
}

static int synobios_get_cpu_temperature(unsigned long arg)
{
	SYNOCPUTEMP temperature = {
		.blSurface = 0,
		.cpu_num = 1,
		.cpu_temp = { synobios_read_cpu_temp(), 0 },
	};

	return synobios_copy_to_user_value(arg, &temperature,
					   sizeof(temperature));
}

static int synobios_get_hwmon_support(unsigned long arg)
{
	SYNO_HWMON_SUPPORT support = {
		.id = HWMON_CPU_TEMP,
		.support = 0,
	};

	if (arg && copy_from_user(&support, (void __user *)arg,
				  sizeof(support)))
		return -EFAULT;

	switch (support.id) {
	case HWMON_CPU_TEMP:
	case HWMON_SYS_THERMAL:
		support.support = 1;
		break;
	case HWMON_FAN_SPEED_RPM:
		support.support = synobios_has_pwm_fan();
		break;
	default:
		support.support = 0;
		break;
	}

	return synobios_copy_to_user_value(arg, &support, sizeof(support));
}

static int synobios_copy_hwmon_sensor(unsigned long arg, const char *type_name,
				      const char *sensor_name,
				      const char *value)
{
	SYNO_HWMON_SENSOR_TYPE sensor = { };

	strscpy(sensor.type_name, type_name, sizeof(sensor.type_name));
	if (sensor_name && value) {
		sensor.sensor_num = 1;
		strscpy(sensor.sensor[0].sensor_name, sensor_name,
			sizeof(sensor.sensor[0].sensor_name));
		strscpy(sensor.sensor[0].value, value,
			sizeof(sensor.sensor[0].value));
	}

	return synobios_copy_to_user_value(arg, &sensor, sizeof(sensor));
}

static long synobios_fake_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	EUNIT_PWRON_TYPE eunit_type = EUNIT_NOT_SUPPORT;
	SYNO_EUP_SUPPORT eup_support = EUP_NOT_SUPPORT;
	char value[MAX_SENSOR_VALUE];

	switch (cmd) {
	case SYNOIO_GETSERIALNUM:
		return synobios_copy_serial_to_user(arg);
	case SYNOIO_SERIAL:
	case SYNOIO_HWHDSUPPORT:
	case SYNOIO_HWTHERMALSUPPORT:
		return synobios_copy_int_to_user(arg, 1);
	case SYNOIO_SYNOVER:
		return synobios_copy_ulong_to_user(arg, 1);
	case SYNOIO_CHECK_MICROP_ID:
		return 0;
	case SYNOIO_BUTTON_POWER:
	case SYNOIO_BUTTON_RESET:
	case SYNOIO_BUTTON_USB:
	case SYNOIO_GET_COPY_BUTTON:
		return synobios_copy_int_to_user(arg, 1);
	case SYNOIO_GET_TEMPERATURE:
		return synobios_copy_int_to_user(arg, synobios_read_sys_temp());
	case SYNOIO_GET_FAN_NUM:
		return synobios_copy_int_to_user(arg, synobios_has_pwm_fan());
	case SYNOIO_GET_HW_CAPABILITY:
		return synobios_get_hw_capability(arg);
	case SYNOIO_GET_CPU_TEMPERATURE:
		return synobios_get_cpu_temperature(arg);
	case SYNOIO_GET_EUNIT_TYPE:
		if (arg && copy_to_user((void __user *)arg, &eunit_type,
					sizeof(eunit_type)))
			return -EFAULT;
		return 0;
	case SYNOIO_IS_FULLY_SUPPORT_EUP:
		if (arg && copy_to_user((void __user *)arg, &eup_support,
					sizeof(eup_support)))
			return -EFAULT;
		return 0;
	case HWMON_GET_SUPPORT:
		return synobios_get_hwmon_support(arg);
	case HWMON_GET_CPU_TEMPERATURE:
		snprintf(value, sizeof(value), "%d", synobios_read_cpu_temp());
		return synobios_copy_hwmon_sensor(arg, HWMON_CPU_TEMP_NAME,
						  "cpu_temp", value);
	case HWMON_GET_FAN_SPEED_RPM:
		if (!synobios_has_pwm_fan())
			return synobios_copy_hwmon_sensor(arg,
							  HWMON_SYS_FAN_RPM_NAME,
							  NULL, NULL);
		snprintf(value, sizeof(value), "%d", synobios_read_fan_rpm());
		return synobios_copy_hwmon_sensor(arg, HWMON_SYS_FAN_RPM_NAME,
						  HWMON_SYS_FAN1_RPM, value);
	case HWMON_GET_SYS_THERMAL:
		snprintf(value, sizeof(value), "%d", synobios_read_sys_temp());
		return synobios_copy_hwmon_sensor(arg, HWMON_SYS_THERMAL_NAME,
						  "temperature", value);
	case HWMON_GET_PSU_STATUS:
		return synobios_copy_hwmon_sensor(arg, HWMON_PSU_STATUS_NAME,
						  NULL, NULL);
	case HWMON_GET_SYS_VOLTAGE:
		return synobios_copy_hwmon_sensor(arg, HWMON_SYS_VOLTAGE_NAME,
						  NULL, NULL);
	case HWMON_GET_HDD_BACKPLANE:
		return synobios_copy_hwmon_sensor(arg, HWMON_HDD_BP_STATUS_NAME,
						  NULL, NULL);
	case HWMON_GET_SYS_CURRENT:
		return synobios_copy_hwmon_sensor(arg, HWMON_SYS_CURRENT_NAME,
						  NULL, NULL);
	default:
		break;
	}

	if ((_IOC_DIR(cmd) & _IOC_READ) != 0)
		return synobios_copy_zero_to_user(arg, _IOC_SIZE(cmd));

	return 0;
}

#ifdef CONFIG_COMPAT
static long synobios_fake_compat_ioctl(struct file *file, unsigned int cmd,
				       unsigned long arg)
{
	return synobios_fake_ioctl(file, cmd, arg);
}
#endif

static __poll_t synobios_fake_poll(struct file *file, poll_table *wait)
{
	return 0;
}

static const struct file_operations synobios_fake_fops = {
	.owner = THIS_MODULE,
	.open = synobios_fake_open,
	.release = synobios_fake_release,
	.unlocked_ioctl = synobios_fake_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = synobios_fake_compat_ioctl,
#endif
	.poll = synobios_fake_poll,
};

static int synobios_proc_cpu_arch_show(struct seq_file *m, void *v)
{
	char cpu_arch[64];

	synobios_get_cpu_arch(cpu_arch, sizeof(cpu_arch));
	seq_printf(m, "%s, %u\n", cpu_arch, num_possible_cpus());

	return 0;
}

static int synobios_proc_crypto_hw_show(struct seq_file *m, void *v)
{
	seq_puts(m, "none\n");
	return 0;
}

static int synobios_proc_platform_show(struct seq_file *m, void *v)
{
	seq_puts(m, "synology_rtd1619b_ds423\n");
	return 0;
}

static int synobios_proc_serial_show(struct seq_file *m, void *v)
{
	char serial[SYNOBIOS_SERIAL_LEN];

	synobios_get_serial(serial, sizeof(serial));
	seq_printf(m, "%s\n", serial);
	return 0;
}

static int synobios_proc_open_cpu_arch(struct inode *inode, struct file *file)
{
	return single_open(file, synobios_proc_cpu_arch_show, NULL);
}

static int synobios_proc_open_crypto_hw(struct inode *inode, struct file *file)
{
	return single_open(file, synobios_proc_crypto_hw_show, NULL);
}

static int synobios_proc_open_platform(struct inode *inode, struct file *file)
{
	return single_open(file, synobios_proc_platform_show, NULL);
}

static int synobios_proc_open_serial(struct inode *inode, struct file *file)
{
	return single_open(file, synobios_proc_serial_show, NULL);
}

static const struct proc_ops synobios_proc_cpu_arch_ops = {
	.proc_open = synobios_proc_open_cpu_arch,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops synobios_proc_crypto_hw_ops = {
	.proc_open = synobios_proc_open_crypto_hw,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops synobios_proc_platform_ops = {
	.proc_open = synobios_proc_open_platform,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops synobios_proc_serial_ops = {
	.proc_open = synobios_proc_open_serial,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static void synobios_fake_proc_init(void)
{
	proc_syno_cpu_arch = proc_create("syno_cpu_arch", 0444, NULL,
					 &synobios_proc_cpu_arch_ops);

	proc_synobios_root = proc_mkdir(SYNOBIOS_NAME, NULL);
	if (!proc_synobios_root)
		return;

	proc_create("crypto_hw", 0444, proc_synobios_root,
		    &synobios_proc_crypto_hw_ops);
	proc_create("syno_platform", 0444, proc_synobios_root,
		    &synobios_proc_platform_ops);
	proc_create("serial", 0444, proc_synobios_root,
		    &synobios_proc_serial_ops);
}

static void synobios_fake_proc_cleanup(void)
{
	if (proc_syno_cpu_arch) {
		remove_proc_entry("syno_cpu_arch", NULL);
		proc_syno_cpu_arch = NULL;
	}

	if (!proc_synobios_root)
		return;

	remove_proc_entry("serial", proc_synobios_root);
	remove_proc_entry("syno_platform", proc_synobios_root);
	remove_proc_entry("crypto_hw", proc_synobios_root);
	remove_proc_entry(SYNOBIOS_NAME, NULL);
	proc_synobios_root = NULL;
}

int syno_append_shutdown_hook(void *hook)
{
	return 0;
}
EXPORT_SYMBOL(syno_append_shutdown_hook);

void save_char_from_uart(unsigned char ch)
{
}
EXPORT_SYMBOL(save_char_from_uart);

void save_current_data_from_uart(void)
{
}
EXPORT_SYMBOL(save_current_data_from_uart);

int synobios_lock_ttyS_current(void)
{
	return 0;
}
EXPORT_SYMBOL(synobios_lock_ttyS_current);

int synobios_lock_ttyS_protection(void)
{
	return 0;
}
EXPORT_SYMBOL(synobios_lock_ttyS_protection);

int try_wakeup_waiting_microp(void)
{
	return 0;
}
EXPORT_SYMBOL(try_wakeup_waiting_microp);

static int __init synobios_fake_init(void)
{
	int ret;

	synobios_init_cpu_info();

	ret = register_chrdev(SYNOBIOS_MAJOR, SYNOBIOS_NAME,
			      &synobios_fake_fops);
	if (ret < 0) {
		pr_err("synobios: can't set major number\n");
		return ret;
	}

	synobios_fake_proc_init();
	func_synobios_event_handler = synobios_fake_event_handler;

	pr_info("synobios: fake load, major number %d\n", SYNOBIOS_MAJOR);
	return 0;
}

static void __exit synobios_fake_exit(void)
{
	if (func_synobios_event_handler == synobios_fake_event_handler)
		func_synobios_event_handler = NULL;

	synobios_fake_proc_cleanup();
	unregister_chrdev(SYNOBIOS_MAJOR, SYNOBIOS_NAME);
	pr_info("synobios: fake unload\n");
}

module_init(synobios_fake_init);
module_exit(synobios_fake_exit);

MODULE_AUTHOR("syno-rockchip-patchkit");
MODULE_DESCRIPTION("Fake Synology synobios compatibility device");
MODULE_LICENSE("GPL");
MODULE_ALIAS("synobios");
