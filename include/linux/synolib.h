// Copyright (c) 2000-2020 Synology Inc. All rights reserved.
#ifndef __SYNOLIB_H_
#define __SYNOLIB_H_

#include <linux/bitops.h>
#include <linux/blk-mq.h>
#ifdef CONFIG_SYNO_MULTIPATH_DEVICE_SYSFS_FORWARD
#include <linux/kobject.h>
#endif /* CONFIG_SYNO_MULTIPATH_DEVICE_SYSFS_FORWARD */

#include <linux/genhd.h>

#ifdef CONFIG_SYNO_MAC_ADDRESS
/* Maximum number of MAC addresses */
#define SYNO_MAC_MAX_NUMBER 8
#endif /* CONFIG_SYNO_MAC_ADDRESS */

#ifdef CONFIG_SYNO_MD_BAD_SECTOR_AUTO_REMAP
void syno_draw_auto_remap_buffer(char *buffer, int size);
#endif /* CONFIG_SYNO_MD_BAD_SECTOR_AUTO_REMAP */

#ifdef CONFIG_SYNO_NVME_DEVICE_INDEX
int SynoNVMeGetDeviceIndex(struct gendisk *disk);
#endif /* CONFIG_SYNO_NVME_DEVICE_INDEX */
#ifdef CONFIG_SYNO_MD_SECTOR_STATUS_REPORT
int syno_disk_get_device_index(struct block_device *bdev);
#endif /* CONFIG_SYNO_MD_SECTOR_STATUS_REPORT */
#ifdef CONFIG_SYNO_PCI_EUNIT_SUPPORT
int syno_pci_dev_to_i2c_bus(struct pci_dev*);
int syno_microp_hdd_auto_enable_manual_disable(int adapter, int address, int iEnable);
struct device_node *syno_pci_dev_to_eunit_node(struct pci_dev *pdev, char *eunit_name);
extern int syno_compare_dts_eunit_pciepath(struct pci_dev *, const struct device_node *);
#endif /* CONFIG_SYNO_PCI_EUNIT_SUPPORT */

#ifdef CONFIG_SYNO_OF
#define DT_INTERNAL_SLOT "internal_slot"
#define DT_STORAGE_SLOT "storage_slot"
#define DT_ESATA_SLOT "esata_port"
#ifdef CONFIG_SYNO_PCI_EUNIT_SUPPORT
#define DT_EUNIT_SLOT "eunit_slot"
#define DT_PCIE_EUNIT_MASTER_PORT "pcie_eunit_master_port"
#define DT_PCIE_EUNIT_NEXT_PORT "pcie_eunit_next_port"
#define DT_PCIE_EUNIT_SSID "pcie_eunit_ssid"
#define DT_PCIE_EUNIT_PORT "pcie_eunit_port"
#define DT_PCIE_EUNIT_DEPTH "pcie_eunit_depth"
#endif /* CONFIG_SYNO_PCI_EUNIT_SUPPORT */
#define DT_PCIE_EUNIT_SLOT "pcie_eunit_slot"
#define DT_PCIE_EUNIT_POSTFIX "pcie_postfix"
#define DT_CX4_SLOT "cx4_port"
#define DT_PCIE_SLOT "pcie_slot"
#ifdef CONFIG_SYNO_PCI_DTS_LABEL
#define DT_LABEL "label"
#define DT_NVME_SLOT "nvme_slot"
#define DT_BUILTIN_GPU "builtin_gpu"
#define DT_M2_CARD "m2_card"
#define DT_NVME_NODE "nvme"
#define DT_PROTOCOL_TYPE "protocol_type"
#define DT_LOCATION "location"
#define DT_ONBOARD_DEVICE "onboard_device"
#define SYNO_PCI_LABEL_MAX_LEN 128

/*
 * Known direct PCIe slot types that have pcie_root in the node itself.
 * When adding a new PCIe slot type, it must be added to this array.
 */
extern const char * const syno_direct_pcie_slot_types[];
#endif /* CONFIG_SYNO_PCI_DTS_LABEL */
#define DT_ETH "eth"
#define DT_USB_SLOT "usb_slot"
#define DT_HUB_SLOT "usb_hub"
#define DT_POWER_PIN_GPIO "power_pin_gpio"
#define DT_DETECT_PIN_GPIO "detect_pin_gpio"
#define DT_SWITCH_NO "switch_no"
#define DT_HDD_LED_TYPE "led_type"
#define DT_HDD_LED_TYPE_LP3943 "lp3943"
#define DT_HDD_LED_TYPE_ATMEGA1608 "atmega1608"
#define DT_HDD_LED_TYPE_GPIO "gpio"
#define DT_HDD_LED_TYPE_TRIG_DISK_SYNO "trig_disk_syno"
#define DT_HDD_ORANGE_LED "led_orange"
#define DT_HDD_GREEN_LED "led_green"
#define DT_HDD_LED_NAME "led_name"
#define DT_HDD_ACT_LED "led_activity"
#define DT_SYNO_GPIO "syno_gpio"
#define DT_PCIE_ROOT "pcie_root"
#define DT_ATA_PORT "ata_port"
#define DT_AHCI "ahci"
#define DT_RTK_AHCI "rtk_ahci"
#define DT_AHCI_MVEBU "ahci_mvebu"
#define DT_MV14XX "mv14xx"
#define DT_VIRTIO "virtio"
#define DT_PHY "phy"
#define DT_USB2 "usb2"
#define DT_USB3 "usb3"
#define DT_USB_PORT "usb_port"
#define DT_USB_HUB "usb_hub"
#define DT_USB_TO_TTY "usb_to_tty"
#define DT_USB_COPY "usb_copy"
#define DT_VBUS "vbus"
#define DT_SHARED "shared"
#define DT_SYNO_SPINUP_GROUP "syno_spinup_group"
#define DT_SYNO_SPINUP_GROUP_DELAY "syno_spinup_group_delay"
#define DT_HDD_POWERUP_SEQ "syno_hdd_powerup_seq"
#define DT_PROPERTY_SW_ACTIVITY "sw_activity"
#define DT_DISK_LED_TYPE_GPIO "gpio"
#define DT_FORM_FACTOR "form_factor"
#define DT_EXPANDER "expander"
#define DT_MODEL_NAME "model_name"
#define DT_SWITCHTEC "switchtec"
#define DT_LED_OFF_GPIO "led_off_gpio"
#define DT_I2C_BUS "i2c_bus"
#define DT_I2C_DEVICE "i2c_device"
#define DT_I2C_ADDRESS "i2c_address"
#define DT_I2C_DEVICE_NAME "i2c_device_name"
#define DT_I2C_REGISTER "i2c_register"
#ifdef CONFIG_SYNO_PCI_DEEP_RETRY
#define DT_I2C_PWR_CTL "i2c_pwr_ctl"
#define DT_I2C_PWR_CTL_ACTION "action"
#define DT_I2C_DELAY_MS "delay"
#define DT_I2C_PWR_CTL_T1 "t1"
#define DT_I2C_PWR_CTL_T3 "t3"
#define DT_I2C_PWR_CTL_T5 "t5"
#define DT_I2C_PWR_CTL_T7 "t7"
#define DT_I2C_PWR_CTL_SIBLING "sibling"
#endif /* CONFIG_SYNO_PCI_DEEP_RETRY */
#define DT_DEVICE_INDEX "device_index"
#define DT_ACPI_HID "acpi_hid"
#define DT_ACPI_UID "acpi_uid"
#define DT_SYNO_I2C_BUS_BIND "syno_i2c_bus_bind"
#define DT_I2C_MUX "i2c_mux"
#define DT_PARENT_BUS "parent_bus"
#define DT_PARENT_ADDR "parent_addr"
#define DT_CHANNEL_ID "channel_id"
#define DT_PHY_ID "phy_id"
#define DT_SAS "sas"
#define DT_PMP_SLOT "pmp_slot"
#define DT_LIBATA "libata"
#define DT_PMP_LINK "pmp_link"
#define DT_TTY_NODE "ttyS"
#define DT_TTY_ADDR_TYPE "addr_type"
#define DT_TTY_TYPE_PCIE "pcie"
#define DT_TTY_TYPE_MMIO "mmio"
#define DT_TTY_TYPE_IO "io"
#define DT_TTY_TYPE_DEV_NAME "dev_name"
#define DT_TTY_SPEED "speed"
#define DT_TTY_CLOCK "clock"
#define DT_TTY_BASE "base"
#define DT_TTY_PREFERRED_CON "preferred_console"
#define DT_TTY_ENABLE_CON "enable_console"
#ifdef CONFIG_SYNO_TTY_DISABLE
#define DT_TTY_DISABLE_CHECK "ttyS0_disable_check"
#endif /* CONFIG_SYNO_TTY_DISABLE */

#define DT_SYSTEM_SLOT "system_slot"
#define DT_MV9XXX "mv9xxx"
#define DT_JMB585 "jmb585"
#define DT_ASM1061 "asm1061"
#define DT_ASM116x "asm116x"
#define DT_SIGNAL_DATA_GEN_FMT "signal_data_gen%d"
#define DT_SET_SSC_OFF "set_ssc_off"

#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
#define DT_SYNO_HDD_SMBUS_TYPE "syno_smbus_hdd_type"
#define DT_SYNO_HDD_SMBUS_ADAPTER "syno_smbus_hdd_adapter"
#define DT_SYNO_HDD_SMBUS_ADDRESS "syno_smbus_hdd_address"
#define DT_SYNO_HDD_SMBUS_PORT "syno_smbus_hdd_port"

#define SMBUS_SWITCH_MAX_COUNT 16
#define DT_SYNO_SMBUS_SWITCH_ADAPTERS "syno_smbus_switch_adapters"
#define DT_SYNO_SMBUS_SWITCH_ADDRS "syno_smbus_switch_addrs"
#define DT_SYNO_SMBUS_SWITCH_VALS "syno_smbus_switch_vals"
#define DT_SYNO_HOST_PRESENT_ADAPTER "syno_host_present_adapter"
#define DT_SYNO_HOST_PRESENT_ADDR "syno_host_present_addr"
#define DT_SYNO_HOST_PRESENT_REG "syno_host_present_reg"
#define DT_SYNO_HOST_PRESENT_VAL "syno_host_present_val"
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

#ifdef CONFIG_SYNO_HWMON_PMBUS
#define DT_SYNO_PMBUS_ADAPTER "syno_pmbus_adapter"
#define DT_SYNO_PMBUS_ADDRESS "syno_pmbus_address"
#define DT_SYNO_PMBUS_PIN_REG "syno_pmbus_pin_register"
#define DT_SYNO_PMBUS_POUT_REG "syno_pmbus_pout_register"
#define DT_SYNO_PMBUS_TEMP1_REG "syno_pmbus_temp1_register"
#define DT_SYNO_PMBUS_TEMP2_REG "syno_pmbus_temp2_register"
#define DT_SYNO_PMBUS_TEMP3_REG "syno_pmbus_temp3_register"
#define DT_SYNO_PMBUS_FAN_REG "syno_pmbus_fan_register"
#define DT_SYNO_PMBUS_STATUS_REG "syno_pmbus_status_register"
#define DT_SYNO_PMBUS_PSU_OFF_BIT "syno_pmbus_psu_off_bit"
#define DT_SYNO_PMBUS_PSU_PRESENT_BIT "syno_pmbus_psu_present_bit"
#endif /* CONFIG_SYNO_HWMON_PMBUS */

#ifdef CONFIG_SYNO_I2C_DW_CLK_FREQ_CUSTOM
#define DT_PROPERTY_I2C_SDA_HOLD_TIME_NS "i2c_sda_hold_time_ns"
#endif /* CONFIG_SYNO_I2C_DW_CLK_FREQ_CUSTOM */

#ifdef CONFIG_SYNO_DISK_POWER_MANAGER
#define DT_SYNO_DISK_POWER_MANAGER "disk_power_manager"
#define SYNO_DPM_UUID_LEN_MAX 64
#define DT_SYNO_DPM_INTERRUPT_EVENT "interrupt_event"
#define DT_SYNO_DPM_EVENT_TYPE "type"
#define DT_SYNO_DPM_EVENT_TYPE_GPIO "gpio"
#define DT_SYNO_DPM_EVENT_TYPE_GPIO_PIN "gpio_pin"
#endif /* CONFIG_SYNO_DISK_POWER_MANAGER */

#ifdef CONFIG_SYNO_MICROP_COMMAND_V2
#define DT_SYNO_MICROP_SERIES "syno_microp_series"
#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */

#ifdef CONFIG_SYNO_AHCI_IRQ_MODE
#define SZ_DTS_AHCI_IRQ "ahci_irq"
#define SZ_AHCI_HARD_IRQ "hard_irq"
#define SZ_AHCI_THREADED_IRQ "threaded_irq"
#endif /* CONFIG_SYNO_AHCI_IRQ_MODE */

#define SZ_DTS_EBOX_I2C_PWR_BTN "power_btn"
#define SZ_DTS_EBOX_I2C_OFFSET "offset"
#define SZ_DTS_EBOX_I2C_MASK "mask"
#define SZ_DTS_EBOX_I2C_PWR_CTL "power_control"
#define SZ_DTS_EBOX_I2C_SN_READ "ebox_sn_read"
#define SZ_DTS_EBOX_RP "rp_power"
#define SZ_DTS_EBOX_RP_INFO "rp_power_info"
#define SZ_DTS_EBOX_I2C_DEEPSELLP_CTL "deep_sleep_control"
#define SZ_DTS_EBOX_I2C_DEEPSELLP_INDICATOR "deep_sleep_indicator"
#define SZ_DTS_EBOX_I2C_REG_MANUAL_ENABLE "reg_manual_enable"

#define SYNO_DTS_PROPERTY_CONTENT_LENGTH 128 // If used to retrive PCIe path, can only accept 9 layer PCIe switch.
#define MAX_NODENAME_LEN 31

#ifdef CONFIG_SYNO_AHCI_INTERNAL_SLOT_MODE
#define DT_AHCI_INTERNAL_MODE "internal_mode"
#endif /* CONFIG_SYNO_AHCI_INTERNAL_SLOT_MODE */

#define DT_SEG7_NUM "seg7_num"
#define DT_SEG7_LED_MAP_0 "seg7_led_map_0"
#define DT_SEG7_LED_MAP_1 "seg7_led_map_1"
#define DT_SEG7_LED_MAP_2 "seg7_led_map_2"

#define DT_REBOOT_DISK_POWER_LOSS "reboot_disk_pwr_lost"

#ifdef CONFIG_SYNO_SAS_HBA_IDX
#define SZ_DTS_NODE_HBA "hba"
#endif /* CONFIG_SYNO_SAS_HBA_IDX */
#define DT_ENCLOSURE "enclosure"
#define DT_MAX_DISK "max_disk"

#ifdef CONFIG_SYNO_I2C_GENERIC_RECOVERY_BY_DTS
#define DT_GPIO_CONTROLLER "gpio_controller"
#define DT_I2C_RCVY_RECOVERY_GPIO "recovery_gpio"
#define DT_I2C_RCVY_SCL "scl"
#define DT_I2C_RCVY_SDA "sda"
#define DT_I2C_RCVY_SCL_MODE_I2C "scl_mode_i2c"
#define DT_I2C_RCVY_SCL_MODE_GPIO "scl_mode_gpio"
#define DT_I2C_RCVY_IOMUX_BASE "iomux_base"
#define DT_I2C_RCVY_IOMUX_BASE_LENG "iomux_base_leng"
#endif /* CONFIG_SYNO_I2C_GENERIC_RECOVERY_BY_DTS */

#define DT_DIMM_SLOT        "dimm_slot"
#define DT_DIMM_LABEL       "label"
#define DT_DIMM_MC_INDEX    "mc"
#define DT_DIMM_LOCATION    "locations"
#define DT_DIMM_SMBIOS_HANDLE "smbios_handle"

/* This enum must sync with synosdk/fs.h for user space having same DISK_PORT_TYPE mapping */
typedef enum _tag_DISK_PORT_TYPE{
	UNKNOWN_DEVICE = 0,
	INTERNAL_DEVICE,
	EXTERNAL_SATA_DEVICE,
	EUNIT_DEVICE,
	EXTERNAL_USB_DEVICE,
	SYNOBOOT_DEVICE,
	ISCSI_DEVICE,
	CACHE_DEVICE,
	USB_HUB_DEVICE,
	SDCARD_DEVICE,
	INVALID_DEVICE,
	SYSTEM_DEVICE,
	DISK_PORT_TYPE_END,
} DISK_PORT_TYPE;

#endif /* CONFIG_SYNO_OF */

#ifdef CONFIG_SYNO_SATA_PWR_CTRL
typedef enum _tag_DISK_PWRCTRL_TYPE {
	PWRCTRL_TYPE_UNKNOWN = 0,
	PWRCTRL_TYPE_GPIO,
	PWRCTRL_TYPE_SMBUS,
	PWRCTRL_TYPE_END,
} DISK_PWRCTRL_TYPE;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL */

#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
typedef struct _syno_smbus_hdd_powerctl {
        bool bl_init;
        int (*syno_smbus_hdd_enable_write)(int adapter, int address, int index, int val);
        int (*syno_smbus_hdd_enable_read)(int adapter, int address, int index);
        int (*syno_smbus_hdd_present_read)(int adapter, int address, int index);
        int (*syno_smbus_hdd_enable_write_all_once)(int adapter, int address);
} SYNO_SMBUS_HDD_POWERCTL;
#define SYNO_MAX_SMBUS_HDD_COUNT 24
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

#ifdef CONFIG_SYNO_SATA_SPINUP_GROUP
#define SYNO_SPINUP_GROUP_MAX 16
#define SYNO_SPINUP_GROUP_PIN_MAX_NUM 8
extern int g_syno_rp_detect_no;
extern int g_syno_rp_detect_list[SYNO_SPINUP_GROUP_PIN_MAX_NUM];
extern int g_syno_hdd_detect_no;
extern int g_syno_hdd_detect_list[SYNO_SPINUP_GROUP_PIN_MAX_NUM];
extern int g_syno_hdd_enable_no;
extern int g_syno_hdd_enable_list[SYNO_SPINUP_GROUP_PIN_MAX_NUM];
#endif /* CONFIG_SYNO_SATA_SPINUP_GROUP */

#ifdef CONFIG_SYNO_PCI_OPTIONAL_SLOT
#define PCI_ADDR_LEN_MAX 9
#define PCI_ADDR_NUM_MAX CONFIG_SYNO_PCI_MAX_SLOT
extern char gszPciAddrList[PCI_ADDR_NUM_MAX][PCI_ADDR_LEN_MAX];
extern int gPciAddrNum;
extern int syno_check_on_option_pci_slot(struct pci_dev *pdev);
#endif /* CONFIG_SYNO_PCI_OPTIONAL_SLOT */

#if defined(CONFIG_SYNO_SATA_ERROR_REPORT) || defined(CONFIG_SYNO_SCSI_DISK_ERROR_REPORT)
#define SYNOBIOS_EVENTDATA_NUM_MAX 8
typedef struct _synobios_event_parm_tag {
	unsigned long long data[SYNOBIOS_EVENTDATA_NUM_MAX];
} SYNOBIOS_EVENT_PARM;

typedef int (*FUNC_SYNOBIOS_EVENT)(SYNOBIOS_EVENT_PARM parms);

typedef struct _synobios_evnet_action_tag {
	unsigned long long synobios_event_type;
	SYNOBIOS_EVENT_PARM parms;
	struct list_head list;
} SYNOBIOS_EVENT_ACTION_LIST;
#endif /* CONFIG_SYNO_SATA_ERROR_REPORT || CONFIG_SYNO_SCSI_DISK_ERROR_REPORT */

#ifdef CONFIG_SYNO_KEXEC_TEST
/*
 * Notice
 * ------
 *  Before calling syno_kexec_test() or reading kexex_test_flags, please
 *  ensure that syno_kexec_test_init() has been called.
 */
#define KEXEC_TEST_DECOMPRESSION	0	/* Did we skip compressed/head_64.S ? */
#define KEXEC_TEST_BOOTLOADER		1	/* Is bootloader type 0xD ? */
#define KEXEC_TEST_E820_TABLE		2	/* Is the minimal start address of usable memory in e820 table 0x100 ? */
#define KEXEC_TEST_SETUP_DATA		3	/* Did we receive setup_data with type SETUP_NONE or SETUP_EFI ? */

extern unsigned long kexec_test_flags;

/*
 * kexec_test_flags initializer.
 */
void __init syno_kexec_test_init(void);
/*
 * Test whether the above KEXEC_TEST_* bits are set.
 */
static __always_inline bool syno_kexec_test(int test)
{
	return 0 != test_bit(test, &kexec_test_flags);
}
#endif /* CONFIG_SYNO_KEXEC_TEST */

#ifdef CONFIG_SYNO_PLUGIN_INTERFACE
/**
 * How to use :
 * 1. module itself register the proprietary instance into the kernel
 *    by a predined MAGIC-key.
 * 2. Others can query the module registration by the same MAGIC-key
 *    and get the instance handle.
 * ********************************************************************
 * Beware of casting/handing "instance", you must know
 * what you are doing before accessing the instance.
 * ********************************************************************
 */
/* For plugin-instance registration */
int syno_plugin_register(int plugin_magic, void *instance);
int syno_plugin_unregister(int plugin_magic);
/* For getting the plugin-instance */
int syno_plugin_handle_get(int plugin_magic, void **hnd);
void * syno_plugin_handle_instance(void *hnd);
void syno_plugin_handle_put(void *hnd);

/* Magic definition */
#define EPIO_PLUGIN_MAGIC_NUMBER    0x20120815
#define RODSP_PLUGIN_MAGIC_NUMBER    0x20141111
#endif /* CONFIG_SYNO_PLUGIN_INTERFACE */

#ifdef CONFIG_SYNO_PCI_DEEP_RETRY
struct pci_dev *syno_root_port_get(struct pci_dev *pdev);
struct pci_dev *syno_get_pci_dev_by_dts_pcie_root(char *szPcieRoot);
int syno_pcie_slot_power_ctrl(struct pci_dev *pdev);
bool syno_check_sriov_enable(struct pci_bus *bus);
bool syno_check_pci_deep_retry_support_by_drv(struct pci_bus *bus);
bool syno_check_pci_power_reset_support(struct pci_bus *bus);
bool syno_pci_deep_retry_support(struct pci_dev *pdev);
void syno_pci_deep_retry(struct pci_dev *pdev);
void syno_pci_probe_failed_retry(struct pci_dev *pdev);
void syno_pci_process_all_failed_devices(void);
int syno_pci_add_probe_failed_device(struct pci_dev *pdev);
#endif /* CONFIG_SYNO_PCI_DEEP_RETRY */

#ifdef CONFIG_SYNO_USB_EUNIT_CONTROL

#define DT_EUNIT_STATUS_EXPSTATUS "expstatus"
#define DT_EUNIT_STATUS_EXPCTRL "expctrl"
#define DT_EUNIT_STATUS_FANPWM "fanpwm"
#define DT_EUNIT_STATUS_FANSPEED "fanspeed"
#define DT_EUNIT_STATUS_HDDCTRL "hddctrl"
#define DT_EUNIT_STATUS_DISKLED "diskled"
#define DT_EUNIT_STATUS_7SEGLED "7segled"
#define DT_EUNIT_STATUS_EXPIDSET "expidset"
#define DT_EUNIT_STATUS_EXPSNSET "expsnset"
#define DT_EUNIT_STATUS_UPVERSION "upversion"
#define DT_EUNIT_STATUS_HDDPRESENT "hddpresent"
#define DT_EUNIT_STATUS_HDDENABLE "hddenable"
#define DT_EUNIT_STATUS_MONCURRENT "moncurrent"
#define DT_EUNIT_STATUS_MONVOLTAGE "monvoltage"
#define DT_EUNIT_STATUS_MONTHERMAL "monthermal"
#define DT_EUNIT_CONTROL_METHOD "control_method"
#define DT_EUNIT_CONTROL_TYPE "control_type"
#define DT_EUNIT_STATUS_POWERMODULE "powermodule"
#define DT_EUNIT_STATUS_EXPBPSNSET "expbpsnset"
#define DT_EUNIT_STATUS_HDDSEDSET "hddsedset"
#define DT_EUNIT_STATUS_ACK "ack"
#define DT_EUNIT_STATUS_HDDRESET "hddreset"
#define DT_EUNIT_STATUS_DIMMER "dimmer"

#define DT_EUNIT_COMMAND "command"
#define DT_EUNIT_OFFSET "offset"

typedef enum _tag_EUNIT_STATUS_INDEX {
	EUNIT_STATUS_EXPCTRL = 0,
	EUNIT_STATUS_FANPWM,
	EUNIT_STATUS_FANSPEED,
	EUNIT_STATUS_HDDCTRL,
	EUNIT_STATUS_DISKLED,
	EUNIT_STATUS_7SEGLED,
	EUNIT_STATUS_EXPSNSET,
	EUNIT_STATUS_EXPIDSET,
	EUNIT_STATUS_UPVERSION,
	EUNIT_STATUS_HDDPRESENT,
	EUNIT_STATUS_HDDENABLE,
	EUNIT_STATUS_MONCURRENT,
	EUNIT_STATUS_MONVOLTAGE,
	EUNIT_STATUS_MONTHERMAL,
	EUNIT_STATUS_POWERMODULE,
	EUNIT_STATUS_EXPBPSNSET,
	EUNIT_STATUS_ACK,
	EUNIT_STATUS_HDDSEDSET,
	EUNIT_STATUS_DIMMER,
	EUNIT_STATUS_UNKNOWN,
	EUNIT_STATUS_INDEX_END,
} EUNIT_STATUS_INDEX;
#endif /* CONFIG_SYNO_USB_EUNIT_CONTROL */

typedef struct {

	int iSlot;
	int iContainer; /* 0: Internal, >0 : Eunit */

} syno_nvme_disk_loc;

#define SYNO_NVME_DISK_IS_INTERNAL(disk) ((disk.iContainer == 0))

struct syno_device_list {
	char disk_name[DISK_NAME_LEN];
	struct list_head device_list;
};

#ifdef CONFIG_SYNO_USB_EUNIT_CONTROL

enum spinup_operation {
	SPINDOWN = 0,
	SPINUP_CHECK,
	SPINUP_NODELAY,
	SPINUP_DELAY,
};

struct syno_control_operations {
	const char control_method[SYNO_DTS_PROPERTY_CONTENT_LENGTH];
	int (*unique_get)(const int slot_type, const int slot_index, char *unique, int unique_size);
	int (*container_index_get_by_diskname)(int *slot_index, const char* diskname);
	int (*deep_sleep_indicator_ctrl)(const int slot_type, const int slot_index, const int ctrl);
	int (*power_control) (const int slot_type, const int slot_index);
	int (*disk_delay_waiting) (const int slot_type, const int slot_index, const int disk_id, int action);
	int (*disk_is_wait_power_on) (const int slot_type, const int slot_index, const int disk_slot_id);
};
#endif /* CONFIG_SYNO_USB_EUNIT_CONTROL */

#ifdef CONFIG_SYNO_MULTIPATH_DEVICE_SYSFS_FORWARD

typedef enum _tag_SYNO_MPATH_SYSFS_AGGR_METHOD {
	/*
	 * The default option.
	 * If the attr values between two native device are the same, we will
	 * arbitrarily pick one of them.
	 */
	MPATH_SYSFS_SHOW_AGGR_ARBITRARY,
	/* Report the minimum value as decimal unsigned long */
	MPATH_SYSFS_SHOW_AGGR_MIN_UL_DEC,
} SYNO_MPATH_SYSFS_SHOW_AGGR_METHOD;

typedef struct _syno_multipath_target_sysfs {
	struct kobject deviceKobj;
	struct kobj_type deviceKtype;
	struct kobject *parent;
	struct mapped_device *md;
	ssize_t (*funcTargetSysfsShow)(struct gendisk*, struct attribute*, char*);
	ssize_t (*funcTargetSysfsStore)(struct gendisk*, struct attribute*, const char*, size_t);
	SYNO_MPATH_SYSFS_SHOW_AGGR_METHOD (*funcTargetShowAggrMethod)(struct attribute*);
} SYNO_MPATH_TARGET_SYSFS;
#endif /* CONFIG_SYNO_MULTIPATH_DEVICE_SYSFS_FORWARD */

#endif //__SYNOLIB_H_
