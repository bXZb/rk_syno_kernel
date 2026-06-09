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

#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/synobios.h>
#include <linux/uaccess.h>

#define SYNOBIOS_NAME "synobios"
#define SYNOBIOS_MAJOR 201
#define SYNOBIOS_MAX_IOCTL_COPY 16384

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
	case CAPABILITY_FAN_RPM_RPT:
		capability.support = 1;
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
		.cpu_temp = { 40, 0 },
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
	case HWMON_FAN_SPEED_RPM:
		support.support = 1;
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

	switch (cmd) {
	case SYNOIO_CHECK_MICROP_ID:
		return 0;
	case SYNOIO_BUTTON_POWER:
	case SYNOIO_BUTTON_RESET:
	case SYNOIO_BUTTON_USB:
	case SYNOIO_GET_COPY_BUTTON:
		return synobios_copy_int_to_user(arg, 1);
	case SYNOIO_GET_TEMPERATURE:
		return synobios_copy_int_to_user(arg, 35);
	case SYNOIO_GET_FAN_NUM:
		return synobios_copy_int_to_user(arg, 1);
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
		return synobios_copy_hwmon_sensor(arg, HWMON_CPU_TEMP_NAME,
						  "cpu_temp", "40");
	case HWMON_GET_FAN_SPEED_RPM:
		return synobios_copy_hwmon_sensor(arg, HWMON_SYS_FAN_RPM_NAME,
						  HWMON_SYS_FAN1_RPM, "1200");
	case HWMON_GET_SYS_THERMAL:
		return synobios_copy_hwmon_sensor(arg, HWMON_SYS_THERMAL_NAME,
						  "temperature", "35");
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
	seq_puts(m, "arm64\n");
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

static void synobios_fake_proc_init(void)
{
	proc_synobios_root = proc_mkdir(SYNOBIOS_NAME, NULL);
	if (!proc_synobios_root)
		return;

	proc_create("cpu_arch", 0444, proc_synobios_root,
		    &synobios_proc_cpu_arch_ops);
	proc_create("crypto_hw", 0444, proc_synobios_root,
		    &synobios_proc_crypto_hw_ops);
	proc_create("syno_platform", 0444, proc_synobios_root,
		    &synobios_proc_platform_ops);
}

static void synobios_fake_proc_cleanup(void)
{
	if (!proc_synobios_root)
		return;

	remove_proc_entry("syno_platform", proc_synobios_root);
	remove_proc_entry("crypto_hw", proc_synobios_root);
	remove_proc_entry("cpu_arch", proc_synobios_root);
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

MODULE_AUTHOR("syno-rk3399-patchkit");
MODULE_DESCRIPTION("Fake Synology synobios compatibility device");
MODULE_LICENSE("GPL");
MODULE_ALIAS("synobios");
