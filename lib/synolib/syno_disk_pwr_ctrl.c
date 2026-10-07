#include <linux/synolib.h>
#include <linux/syno_gpio.h>
#include <linux/synobios.h>

#ifdef CONFIG_SYNO_SATA_SPINUP_GROUP
#include <linux/synosata.h>
#endif /* CONFIG_SYNO_SATA_SPINUP_GROUP */

#ifdef CONFIG_SYNO_SATA_PWR_CTRL_GPIO
static int syno_hdd_poweron_gpio(int index, int value)
{
	if (!HAVE_HDD_ENABLE(index)) { // index is 1-based
		printk("No such hdd enable pin. Index: %d\n", index);
		WARN_ON(1);
		return -EINVAL;
	}
	SYNO_GPIO_WRITE(HDD_ENABLE_PIN(index), value);
	return 0;
}

static int syno_hdd_enable_gpio(int index)
{
	int ret = 0; /* default is not enable */

	if (!HAVE_HDD_ENABLE(index)) { // index is 1-based
		goto END;
	}
	ret = SYNO_GPIO_READ(HDD_ENABLE_PIN(index));
	/* hdd enable pin is low active so the result must be inverted*/
	if (ACTIVE_LOW == HDD_ENABLE_POLARITY(index)) {
		ret = !ret;
	}
END:
	return ret;
}

static int syno_hdd_detect_gpio(int index)
{
	int ret = 1; /* default is present */

	if (!HAVE_HDD_DETECT(index)) { // index is 1-based
		goto END;
	}
	ret = SYNO_GPIO_READ(HDD_DETECT_PIN(index));
	/* hdd detect pin is low active so the result must be inverted*/
	if (ACTIVE_LOW == HDD_DETECT_POLARITY(index)) {
		ret = !ret;
	}
END:
	return ret;
}
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_GPIO */

#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
extern long g_smbus_hdd_powerctl;
extern int gSynoSmbusHddAdapter;
extern int gSynoSmbusHddAddress;
extern void syno_smbus_hdd_powerctl_init(void);
extern SYNO_SMBUS_HDD_POWERCTL SynoSmbusHddPowerCtl;

static int syno_hdd_poweron_smbus(int index, int value)
{
	if (!SynoSmbusHddPowerCtl.bl_init){
		syno_smbus_hdd_powerctl_init();
	}

	if (NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write) {
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write(gSynoSmbusHddAdapter, gSynoSmbusHddAddress, index, value);
	}

	return 0;
}

static int syno_hdd_enable_smbus(int index)
{
	int ret = 0; /*defult is not enable*/

	if (!SynoSmbusHddPowerCtl.bl_init){
		syno_smbus_hdd_powerctl_init();
	}

	if ( NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read) {
		ret = SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read(gSynoSmbusHddAdapter, gSynoSmbusHddAddress, index);
	}

	return ret;
}

static int syno_hdd_detect_smbus(int index)
{
	int ret = 0; /*defult is not present*/

	if (!SynoSmbusHddPowerCtl.bl_init){
		syno_smbus_hdd_powerctl_init();
	}

	if ( NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read) {
		ret = SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read(gSynoSmbusHddAdapter, gSynoSmbusHddAddress, index);
	}

	return ret;
}

static int syno_hdd_poweron_smbus_all_once(void)
{
	if (!SynoSmbusHddPowerCtl.bl_init){
		syno_smbus_hdd_powerctl_init();
	}

	if (NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write_all_once) {
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write_all_once(gSynoSmbusHddAdapter, gSynoSmbusHddAddress);
	}

	return 0;
}
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

DISK_PWRCTRL_TYPE SYNO_GET_DISK_PWR_TYPE(int index)
{
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_GPIO
	/* Check GPIO */
	if (HAVE_HDD_ENABLE(index)) {
		return PWRCTRL_TYPE_GPIO;
	}
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_GPIO */
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
	/* Check SMBUS */
	if (0 < g_smbus_hdd_powerctl) {
		return PWRCTRL_TYPE_SMBUS;
	}
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

	return PWRCTRL_TYPE_UNKNOWN;
}
EXPORT_SYMBOL(SYNO_GET_DISK_PWR_TYPE);

/* SYNO_CTRL_HDD_POWERON - Power control of internal disk
 * @index: disk index
 * @value: 1 poweron
 *	   0 poweroff
 * @return: 0 success
 *	    1 failed
 */
int SYNO_CTRL_HDD_POWERON(int index, int value)
{
	int ret = 0;

	switch (SYNO_GET_DISK_PWR_TYPE(index)) {
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_GPIO
		case PWRCTRL_TYPE_GPIO:
			ret = syno_hdd_poweron_gpio(index, value);
			break;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_GPIO */
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
		case PWRCTRL_TYPE_SMBUS:
			ret = syno_hdd_poweron_smbus(index, value);
			break;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
		default:
			ret = 0;
	}
	return ret;
}
EXPORT_SYMBOL(SYNO_CTRL_HDD_POWERON);

/* SYNO_CHECK_HDD_ENABLE
 * Query HDD enable check .
 * output: 1 - enable, 0 - not enable.
 */
int SYNO_CHECK_HDD_ENABLE(int index)
{
	int ret = 0;

	switch (SYNO_GET_DISK_PWR_TYPE(index)) {
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_GPIO
		case PWRCTRL_TYPE_GPIO:
			ret = syno_hdd_enable_gpio(index);
			break;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_GPIO */
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
		case PWRCTRL_TYPE_SMBUS:
			ret = syno_hdd_enable_smbus(index);
			break;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
		default:
			ret = 0;
	}
	return ret;
}
EXPORT_SYMBOL(SYNO_CHECK_HDD_ENABLE);

/* SYNO_CHECK_HDD_DETECT
 * Query HDD present check .
 * output: 1 - present, 0 - not present.
 */
int SYNO_CHECK_HDD_DETECT(int index)
{
	int ret = 0;

	switch (SYNO_GET_DISK_PWR_TYPE(index)) {
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_GPIO
		case PWRCTRL_TYPE_GPIO:
			ret = syno_hdd_detect_gpio(index);
			break;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_GPIO */
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
		case PWRCTRL_TYPE_SMBUS:
			ret = syno_hdd_detect_smbus(index);
			break;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
		default:
			ret = 0;
	}
	return ret;
}
EXPORT_SYMBOL(SYNO_CHECK_HDD_DETECT);

/* SYNO_SUPPORT_HDD_DYNAMIC_ENABLE_POWER
 * Query support HDD dynamic Power .
 * output: 1 - support, 0 - not support.
 */
int SYNO_SUPPORT_HDD_DYNAMIC_ENABLE_POWER(int index)
{
	int ret = 0;

	switch (SYNO_GET_DISK_PWR_TYPE(index))
	{
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_GPIO
		case PWRCTRL_TYPE_GPIO:
			ret = 1;
			break;
#endif /* #ifdef CONFIG_SYNO_SATA_PWR_CTRL_GPIO */
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
		case PWRCTRL_TYPE_SMBUS:
			if (!SynoSmbusHddPowerCtl.bl_init){
				syno_smbus_hdd_powerctl_init();
			}
			if(NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write) {
				ret = 1;
			}
			break;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
		default:
			ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL(SYNO_SUPPORT_HDD_DYNAMIC_ENABLE_POWER);

/* SYNO_HDD_POWER_ON - Power control for internal disk
 * @index: disk slot index
 *
 * Special case for 1621+:
 *      Need to poweron all disks at the same time.
 */
void SYNO_HDD_POWER_ON(int index)
{
	switch (SYNO_GET_DISK_PWR_TYPE(index)) {
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_GPIO
		case PWRCTRL_TYPE_GPIO:
			/* Power on the disk if it has presented. */
			if (1 == SYNO_CHECK_HDD_DETECT(index)) {
#ifdef CONFIG_SYNO_SATA_SPINUP_GROUP
				DBG_SpinupGroup("Power on disk: %d\n", index);
#endif /* CONFIG_SYNO_SATA_SPINUP_GROUP */
				SYNO_CTRL_HDD_POWERON(index, 1);
			}
			break;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_GPIO */
#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
		case PWRCTRL_TYPE_SMBUS:
#ifdef CONFIG_SYNO_HW_VERSION
			/* Special case: DS1621+ */
			if (syno_is_hw_version(HW_DS1621p)) {
				syno_hdd_poweron_smbus_all_once();
				break;
			}
#endif /* CONFIG_SYNO_HW_VERSION */
#ifdef CONFIG_SYNO_SATA_SPINUP_GROUP
			DBG_SpinupGroup("Power on disk: %d\n", index);
#endif /* CONFIG_SYNO_SATA_SPINUP_GROUP */
			SYNO_CTRL_HDD_POWERON(index, 1);
			break;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
		default:
			break;
	}

	return;
}

EXPORT_SYMBOL(SYNO_HDD_POWER_ON);
