#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2022 Synology Inc. All rights reserved.
#ifndef _UAPI_LINUX_SYNOBIOS_H
#define _UAPI_LINUX_SYNOBIOS_H

#include <linux/syno_model_info.h>

#define SYNO_EBOX_UNIQUE_MAX_LEN 16

struct syno_ebox_unique_id {
	int uniqueId;
	int mask;
	char szUnique[SYNO_EBOX_UNIQUE_MAX_LEN];
	char szUniqueRp[SYNO_EBOX_UNIQUE_MAX_LEN];
};

static const struct syno_ebox_unique_id syno_ebox_unique_mapping[] = {
	{0x34, 0x7c, EBOX_INFO_UNIQUE_RX410, EBOX_INFO_UNIQUE_RX410}, 		// RX410, 	0x34 ~ 0x37
	{0x54, 0x7c, EBOX_INFO_UNIQUE_RX4, EBOX_INFO_UNIQUE_RX4}, 			// RX4, 	0x54 ~ 0x57
	{0x18, 0x7c, EBOX_INFO_UNIQUE_DX513, EBOX_INFO_UNIQUE_DX513}, 		// DX513, 	0x18 ~ 0x1b
	{0x68, 0x7c, EBOX_INFO_UNIQUE_DX510, EBOX_INFO_UNIQUE_DX510}, 		// DX510, 	0x68 ~ 0x6b
	{0x28, 0x7c, EBOX_INFO_UNIQUE_DX5, EBOX_INFO_UNIQUE_DX5}, 			// DX5, 	0x28 ~ 0x2b
	{0x4c, 0x7c, EBOX_INFO_UNIQUE_DXC, EBOX_INFO_UNIQUE_DXC}, 			// DXC, 	0x4c ~ 0x4f
	{0x2c, 0x7c, EBOX_INFO_UNIQUE_RXC, EBOX_INFO_UNIQUE_RXCRP}, 		// RXC or RXCRP, 0x2c ~ 0x2f
	{0x58, 0x7c, EBOX_INFO_UNIQUE_DX213, EBOX_INFO_UNIQUE_DX213}, 		// DX213, 	0x58 ~ 0x5b 
	{0x11, 0x1f, EBOX_INFO_UNIQUE_RX413, EBOX_INFO_UNIQUE_RX413}, 		// RX413, 	0x11
	{0x12, 0x1f, EBOX_INFO_UNIQUE_RX1214, EBOX_INFO_UNIQUE_RX1214RP}, 	// RX1214 or RX1214RP, 0x12
	{0x14, 0x1f, EBOX_INFO_UNIQUE_RX1217, EBOX_INFO_UNIQUE_RX1217RP},	// RX1217 or RX1217RP, 0x14
	{0x54, 0x7c, EBOX_INFO_UNIQUE_RX415, EBOX_INFO_UNIQUE_RX415},		// RX415, 	0x54 ~ 0x57
	{0x13, 0x1f, EBOX_INFO_UNIQUE_DX1215, EBOX_INFO_UNIQUE_DX1215},		// DX1215, 	0x13
	{0x15, 0x1f, EBOX_INFO_UNIQUE_DX517, EBOX_INFO_UNIQUE_DX517},		// DX517, 	0x15
	{0x16, 0x1f, EBOX_INFO_UNIQUE_RX418, EBOX_INFO_UNIQUE_RX418},		// RX418, 	0x16
	{0x17, 0x1f, EBOX_INFO_UNIQUE_DX1222, EBOX_INFO_UNIQUE_DX1222},		// DX1222, 	0x17
	{0x1C, 0x1f, EBOX_INFO_UNIQUE_DX1215II, EBOX_INFO_UNIQUE_DX1215II},	// DX1215II, 0x1C
	{0x01, 0x1f, EBOX_INFO_UNIQUE_RX1223RP, EBOX_INFO_UNIQUE_RX1223RP},	// RX1223RP, 0x01
	
	{ }, /* terminate list */
};



#define HWMON_CPU_TEMP_NAME "CPU_Temperature"
#define HWMON_SYS_THERMAL_NAME "System_Thermal_Sensor"
#define HWMON_SYS_VOLTAGE_NAME "System_Voltage_Sensor"
#define HWMON_SYS_FAN_RPM_NAME "System_Fan_Speed_RPM"
#define HWMON_SYS_CURRENT_NAME "System_Current_Sensor"
#define HWMON_SYS_FAN1_RPM "fan1_rpm"
#define HWMON_SYS_FAN2_RPM "fan2_rpm"
#define HWMON_SYS_FAN3_RPM "fan3_rpm"
#define HWMON_SYS_FAN4_RPM "fan4_rpm"
#define HWMON_PSU_STATUS_NAME "PSU_%d_Status"
#define HWMON_PSU1_STATUS_NAME "PSU_1_Status"
#define HWMON_PSU2_STATUS_NAME "PSU_2_Status"
#define HWMON_PSU_SENSOR_PIN "power_in"
#define HWMON_PSU_SENSOR_POUT "power_out"
#define HWMON_PSU_SENSOR_TEMP "temperature"
#define HWMON_PSU_SENSOR_TEMP1 "temperature_1"
#define HWMON_PSU_SENSOR_TEMP2 "temperature_2"
#define HWMON_PSU_SENSOR_TEMP3 "temperature_3"
#define HWMON_PSU_SENSOR_FAN "fan_speed"
#define HWMON_PSU_SENSOR_FAN_VOLT "fan_voltage"
#define HWMON_PSU_SENSOR_STATUS "status"
#define HWMON_HDD_BP_STATUS_NAME "HDD_Backplane_Status"
#define HWMON_HDD_BP_DETECT "hdd_detect"
#define HWMON_HDD_BP_ENABLE "hdd_enable"
#define HWMON_HDD_BP_INTF "hdd_intf"
#define MAX_SENSOR_NUM 10
#define MAX_SENSOR_NAME 30
#define MAX_SENSOR_VALUE 30

typedef enum _tag_EUNIT_PWRON_TYPE {
	EUNIT_NOT_SUPPORT,
	EUNIT_PWRON_GPIO,
	EUNIT_PWRON_ATACMD,
	EUNIT_SAS_NO_PWRON,
} EUNIT_PWRON_TYPE;

typedef enum {
	CAPABILITY_THERMAL      = 1,
	CAPABILITY_DISK_LED_CTRL= 2,
	CAPABILITY_AUTO_POWERON = 3,
	CAPABILITY_CPU_TEMP     = 4,
	CAPABILITY_S_LED_BREATH = 5,
	CAPABILITY_FAN_RPM_RPT  = 6,
	CAPABILITY_MICROP_PWM   = 7,
	CAPABILITY_CARDREADER   = 8,
	CAPABILITY_LCM          = 9,
	CAPABILITY_NONE         = -1,
} SYNO_HW_CAPABILITY;

typedef struct _tag_CAPABILITY {
	SYNO_HW_CAPABILITY      id;
	int support;
} CAPABILITY;

typedef enum {
	DISK_LED_OFF = 0,
	DISK_LED_GREEN_SOLID,
	DISK_LED_ORANGE_SOLID,
	DISK_LED_ORANGE_BLINK,
	DISK_LED_GREEN_BLINK,
} SYNO_DISK_LED;

typedef struct _tag_SYNO_SUPERIO_PACKAGE{
	unsigned char ldn;
	unsigned char reg;
	unsigned char value;
} SYNO_SUPERIO_PACKAGE;

typedef enum {
	EUP_FULLY_SUPPORT = 0,
	EUP_NOT_FULLY_SUPPORT,
	EUP_NOT_SUPPORT,
} SYNO_EUP_SUPPORT;

#define MAX_CPU 2
typedef struct _SynoCpuTemp {
	unsigned char blSurface;
	int cpu_num;
	int cpu_temp[MAX_CPU];
} SYNOCPUTEMP;

typedef struct _SynoThermalTemp {
	unsigned char blSurface;
	int temperature;
} SYNO_THERMAL_TEMP;

typedef enum {
	HWMON_CPU_TEMP,
	HWMON_SYS_THERMAL,
	HWMON_SYS_VOLTAGE,
	HWMON_FAN_SPEED_RPM,
	HWMON_HDD_BACKPLANE,
	HWMON_PSU_STATUS,
	HWMON_SYS_CURRENT,
} SYNO_HWMON_SUPPORT_ID;

typedef struct _SYNO_HWMON_SUPPORT {
	SYNO_HWMON_SUPPORT_ID id;
	int support;
} SYNO_HWMON_SUPPORT;

typedef struct _SYNO_HWMON_SENSOR {
	char sensor_name[MAX_SENSOR_NAME];
	char value[MAX_SENSOR_VALUE];
} SYNO_HWMON_SENSOR;

typedef struct _SYNO_HWMON_SENSOR_TYPE {
	char type_name[MAX_SENSOR_NAME];
	int sensor_num;
	SYNO_HWMON_SENSOR sensor[MAX_SENSOR_NUM];
} SYNO_HWMON_SENSOR_TYPE;

typedef struct _SYNO_HWMON_FAN_ORDER {
	int fan_num;
	int fan_order_list[MAX_SENSOR_NUM];
} SYNO_HWMON_FAN_ORDER;

enum {
    MD_SECTOR_READ_ERROR = 0,
    MD_SECTOR_WRITE_ERROR = 1,
    MD_SECTOR_REWRITE_OK = 2,
    MD_FAULTY_DEVICE = 3,
};

typedef enum {
	SYNO_SCSI_UNKNOWN = 0,
	SYNO_SCSI_ERROR_WITH_SENSE,
	SYNO_SCSI_ERROR_TIMEOUT,
} SYNO_SCSI_ERROR_EVENT_TYPE;

typedef enum {
	SYNO_LED_OFF = 0,
	SYNO_LED_ON,
	SYNO_LED_BLINKING,
} SYNO_LED;

/* TODO: Because user space also need this define, so we define them here.
 * But userspace didn't have a common define like MY_DEF_HERE include
 * kernel space. So we can't define it inside some define */
#define EBOX_GPIO_KEY           "gpio"
#define EBOX_I2C_KEY            "i2c"
#define EBOX_I2C_POLLING_FAN	"fanTach"
#define EBOX_I2C_SYSFS_OP_WRITE "Write"
#define EBOX_I2C_SYSFS_OP_READ  "Read"
#define EBOX_I2C_SYSFS_OP_POLL  "Poll"
#define EBOX_I2C_SYSFS_OP_JMB575_LED_CTL "JMB575_LED_Ctrl"
#define EBOX_INFO_DEV_LIST_KEY  "syno_device_list"
#define EBOX_INFO_VENDOR_KEY    "vendorid"
#define EBOX_INFO_DEVICE_KEY    "deviceid"
#define EBOX_INFO_ERROR_HANDLE  "error_handle"
#define EBOX_INFO_UNIQUE_KEY    "Unique"
#define EBOX_INFO_EMID_KEY      "EMID"
#define EBOX_INFO_SATAHOST_KEY  "sata_host"
#define EBOX_INFO_PORTNO_KEY    "port_no"
#define EBOX_INFO_PCIEPATH_KEY  "pciepath"
#define EBOX_INFO_CPLDVER_KEY   "cpld_version"
#define EBOX_INFO_DEEP_SLEEP    "deepsleep_support"
#define EBOX_INFO_IRQ_OFF       "irq_off"
#define EBOX_INFO_PHY_KEY       "phy" /* for mv14xx */

/* Use 'K' as magic number */
#define SYNOBIOS_IOC_MAGIC  'K'

#define SYNOIO_GET_EUNIT_TYPE     _IOR(SYNOBIOS_IOC_MAGIC, 41, EUNIT_PWRON_TYPE)
#define SYNOIO_SUPERIO_READ       _IOWR(SYNOBIOS_IOC_MAGIC, 216, SYNO_SUPERIO_PACKAGE)
#define SYNOIO_SUPERIO_WRITE      _IOWR(SYNOBIOS_IOC_MAGIC, 217, SYNO_SUPERIO_PACKAGE)
#define SYNOIO_IS_FULLY_SUPPORT_EUP _IOWR(SYNOBIOS_IOC_MAGIC, 218, SYNO_EUP_SUPPORT)

#ifdef MY_ABC_HERE
extern int syno_is_hw_version(const char *hw_version);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
extern int syno_is_hw_revision(const char *hw_revision);
#endif /* MY_ABC_HERE */

#define SYNO_MICROP_TTY_NAME    "ttyS1"

#ifdef MY_ABC_HERE
extern int (*func_synobios_event_handler)(unsigned long long, ...);
#endif /* MY_ABC_HERE */

#define SYNO_EVENT_USB_PROHIBIT           0x1b00
#define SYNO_EVENT_CONSOLE_PROHIBIT       0x1c00
#define SYNO_EVENT_MICROP_GET             0x1d00
#define SYNO_EVENT_DISK_PWR_RESET         0x2700
#define SYNO_EVENT_DISK_PORT_DISABLED     0x2800
#define SYNO_EVENT_SATA_ERROR_REPORT      0x2a00
#define SYNO_EVENT_WAKE_FROM_DEEP_SLEEP   0x2b00
#define SYNO_EVENT_DISK_RETRY_REPORT      0x2c00
#define SYNO_EVENT_DSIK_POWER_SHORT_BREAK 0x2e00
#define SYNO_EVENT_DISK_TIMEOUT_REPORT    0x3000
#define SYNO_EVENT_DISK_RESET_FAIL_REPORT 0x3100
#define SYNO_EVENT_DISK_PORT_LOST         0x3200
#define SYNO_EVENT_SCSI_ERROR             0x3300
#define SYNO_EVENT_EBOX_RESET			  0x3400

#endif  /* _UAPI_LINUX_SYNOBIOS_H */
