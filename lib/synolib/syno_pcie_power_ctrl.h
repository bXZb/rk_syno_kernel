/* Copyright (c) 2000-2024 Synology Inc. All rights reserved. */
#ifndef __SYNO_PCIE_POWER_CTRL_H__
#define __SYNO_PCIE_POWER_CTRL_H__

#include <linux/pci.h>
#include <linux/list.h>
#include <linux/i2c.h>

#define DT_I2C_PWR_CTL_SIBLING_STR_LEN 128
#define I2C_PWR_CTL_T1_REG 0x07
#define I2C_PWR_CTL_T3_REG 0x08
#define I2C_PWR_CTL_T5_REG 0x0A
#define I2C_PWR_CTL_T7_REG 0x09
#define I2C_PWR_CTL_RESET_ACTION 0x02

#define SYNO_MAX_DELAY_PERIOD 10000 // 10000 ms
#define SYNO_PCI_READY_TIMEOUT 180 // 180s

struct syno_pci_pwr_ctrl_st {
	int i2c_bus;
	int i2c_addr;
	int i2c_register;
	int i2c_action;
	int i2c_delay;
	int i2c_t1;
	int i2c_t3;
	int i2c_t5;
	int i2c_t7;
	char i2c_sibling[DT_I2C_PWR_CTL_SIBLING_STR_LEN];
};

typedef enum {
	SYNO_PCIE_RESET_PCIE_SLOT,
	SYNO_PCIE_RESET_ETH,
	SYNO_PCIE_RESET_NOT_SUPPORT,
} syno_pci_reset_t;

struct syno_pcie_list {
	struct pci_dev *pdev;
	struct list_head list;
};

/* External function declarations */
struct pci_dev *syno_pci_slot_dev_get(struct pci_dev *pdev);
struct pci_dev *syno_root_port_get(struct pci_dev *pdev);

/* Internal power control functions - used by multiple files */
int syno_pcie_slot_get_pwr_ctrl_info(struct pci_dev *pdev,
				     struct syno_pci_pwr_ctrl_st *pci_pwr_ctrl);
int syno_pcie_slot_perform_i2c_time_checks(
	struct pci_dev *pdev, struct i2c_adapter *pAdapter,
	struct syno_pci_pwr_ctrl_st *pci_pwr_ctrl);
int syno_pcie_slot_execute_power_action(
	struct i2c_adapter *pAdapter,
	struct syno_pci_pwr_ctrl_st *pci_pwr_ctrl);
bool syno_pci_power_reset_support(struct pci_dev *pdev);
void syno_pci_deep_retry_find_sibling(struct pci_dev *pdev,
				      struct list_head *pci_list_head);
int syno_collect_pci_devices(struct pci_dev *pdev, void *data);

#endif /* __SYNO_PCIE_POWER_CTRL_H__ */