/* Copyright (c) 2000-2024 Synology Inc. All rights reserved. */
#include <linux/synolib.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include "syno_pcie_power_ctrl.h"

extern int syno_compare_dts_pciepath_partial(struct pci_dev *pdev, const struct device_node *pDeviceNode);
extern struct pci_dev *syno_pcie_slot_dev_get(struct pci_dev *pdev);

static int find_pcie_dev_path_by_eth(int eth_idx, char *szPciePath, int len)
{
	struct device_node *eth_node = NULL;
	char *szDtsNodePciePath = NULL;
	int index = -1;
	int ret = -1;

	if (NULL == szPciePath || 0 >= len) {
		goto END;
	}

	for_each_child_of_node(of_root, eth_node) {
		if (!eth_node->full_name || 1 != sscanf(eth_node->full_name, DT_ETH "@%d", &index)) {
			continue;
		}
		if (eth_idx == index) {
			break;
		}
	}
	if (NULL == eth_node) {
		goto END;
	}

	szDtsNodePciePath = (char *)of_get_property(eth_node, DT_PCIE_ROOT, NULL);
	if (szDtsNodePciePath) {
		snprintf(szPciePath, len, "%s", szDtsNodePciePath);
	}
	ret = 0;

END:
	if (eth_node) {
		of_node_put(eth_node);
	}
	return ret;
}

static int syno_slot_info_get(struct pci_dev *pdev, struct syno_pci_pwr_ctrl_st *pci_pwr_ctrl, const char *type)
{
	struct device_node *pcie_slot_node = NULL;
	struct device_node *i2c_pwr_ctl_node = NULL;
	int ret = -1;
	int i2c_bus = 0;
	int i2c_addr = 0;
	int i2c_register = 0;
	int i2c_action = 0;
	int i2c_delay = 0;
	int i2c_t1 = 0;
	int i2c_t3 = 0;
	int i2c_t5 = 0;
	int i2c_t7 = 0;
	const char *pSibling;

	if (!pdev || !pci_pwr_ctrl || !type) {
		pci_err(pdev, "%s Parameter error\n", __func__);
		goto END;
	}

	for_each_child_of_node(of_root, pcie_slot_node) {
		if (pcie_slot_node->full_name && 0 == strncmp(pcie_slot_node->full_name, type, strlen(type))) {
			if (0 != syno_compare_dts_pciepath_partial(pdev, pcie_slot_node)) {
				continue;
			}
			i2c_pwr_ctl_node = of_get_child_by_name(pcie_slot_node, DT_I2C_PWR_CTL);
			if (NULL == i2c_pwr_ctl_node) {
				continue;
			}
			if (0 != of_property_read_u32_index(i2c_pwr_ctl_node, DT_I2C_BUS, 0, &i2c_bus)) {
				pci_err(pdev, "%s reading i2c bus failed.\n", __func__);
				goto PUT_NODE;
			}
			if (0 != of_property_read_u32_index(i2c_pwr_ctl_node, DT_I2C_ADDRESS, 0, &i2c_addr)) {
				pci_err(pdev, "%s reading i2c address failed.\n", __func__);
				goto PUT_NODE;
			}
			if (0 != of_property_read_u32_index(i2c_pwr_ctl_node, DT_I2C_REGISTER, 0, &i2c_register)) {
				pci_err(pdev, "%s reading i2c register failed.\n", __func__);
				goto PUT_NODE;
			}
			if (0 != of_property_read_u32_index(i2c_pwr_ctl_node, DT_I2C_PWR_CTL_ACTION, 0, &i2c_action)) {
				pci_err(pdev, "%s reading i2c action off failed.\n", __func__);
				goto PUT_NODE;
			}
			if (0 != of_property_read_u32_index(i2c_pwr_ctl_node, DT_I2C_DELAY_MS, 0, &i2c_delay)) {
				pci_err(pdev, "%s reading i2c delay failed.\n", __func__);
				goto PUT_NODE;
			}
			// optional
			if (of_find_property(i2c_pwr_ctl_node, DT_I2C_PWR_CTL_T1, NULL) &&
			    0 != of_property_read_u32_index(i2c_pwr_ctl_node, DT_I2C_PWR_CTL_T1, 0, &i2c_t1)) {
				pci_err(pdev, "%s reading i2c t1 failed.\n", __func__);
				goto PUT_NODE;
			}
			// optional
			if (of_find_property(i2c_pwr_ctl_node, DT_I2C_PWR_CTL_T3, NULL) &&
			    0 != of_property_read_u32_index(i2c_pwr_ctl_node, DT_I2C_PWR_CTL_T3, 0, &i2c_t3)) {
				pci_err(pdev, "%s reading i2c t3 failed.\n", __func__);
				goto PUT_NODE;
			}
			// optional
			if (of_find_property(i2c_pwr_ctl_node, DT_I2C_PWR_CTL_T5, NULL) &&
			    0 != of_property_read_u32_index(i2c_pwr_ctl_node, DT_I2C_PWR_CTL_T5, 0, &i2c_t5)) {
				pci_err(pdev, "%s reading i2c t5 failed.\n", __func__);
				goto PUT_NODE;
			}
			// optional
			if (of_find_property(i2c_pwr_ctl_node, DT_I2C_PWR_CTL_T7, NULL) &&
			    0 != of_property_read_u32_index(i2c_pwr_ctl_node, DT_I2C_PWR_CTL_T7, 0, &i2c_t7)) {
				pci_err(pdev, "%s reading i2c t7 failed.\n", __func__);
				goto PUT_NODE;
			}

			pci_pwr_ctrl->i2c_bus = i2c_bus;
			pci_pwr_ctrl->i2c_addr = i2c_addr;
			pci_pwr_ctrl->i2c_register = i2c_register;
			pci_pwr_ctrl->i2c_action = i2c_action;
			pci_pwr_ctrl->i2c_delay = i2c_delay;
			pci_pwr_ctrl->i2c_t1 = i2c_t1;
			pci_pwr_ctrl->i2c_t3 = i2c_t3;
			pci_pwr_ctrl->i2c_t5 = i2c_t5;
			pci_pwr_ctrl->i2c_t7 = i2c_t7;

			// optional
			pSibling = (char *)of_get_property(i2c_pwr_ctl_node, DT_I2C_PWR_CTL_SIBLING, NULL);
			if (pSibling) {
				snprintf(pci_pwr_ctrl->i2c_sibling, sizeof(pci_pwr_ctrl->i2c_sibling), "%s", pSibling);
			}

			ret = 0;
PUT_NODE:
			if (i2c_pwr_ctl_node) {
				of_node_put(i2c_pwr_ctl_node);
			}
			break;
		}
	}

END:
	if (pcie_slot_node) {
		of_node_put(pcie_slot_node);
	}
	return ret;
}

static int syno_pci_slot_info_get(struct pci_dev *pdev, struct syno_pci_pwr_ctrl_st *pci_pwr_ctrl)
{
	return syno_slot_info_get(pdev, pci_pwr_ctrl, DT_PCIE_SLOT);
}

static int syno_eth_slot_info_get(struct pci_dev *pdev, struct syno_pci_pwr_ctrl_st *pci_pwr_ctrl)
{
	return syno_slot_info_get(pdev, pci_pwr_ctrl, DT_ETH);
}

static syno_pci_reset_t syno_pci_slot_power_reset_support(struct pci_dev *pdev)
{
	struct syno_pci_pwr_ctrl_st pci_pwr_ctrl;
	syno_pci_reset_t ret = SYNO_PCIE_RESET_NOT_SUPPORT;

	if (!pdev) {
		pci_err(pdev, "%s Parameter error\n", __func__);
		goto END;
	}

	memset(&pci_pwr_ctrl, 0, sizeof(struct syno_pci_pwr_ctrl_st));

	if (0 == syno_pci_slot_info_get(pdev, &pci_pwr_ctrl)) {
		ret = SYNO_PCIE_RESET_PCIE_SLOT;
		goto END;
	}

	if (0 == syno_eth_slot_info_get(pdev, &pci_pwr_ctrl)) {
		ret = SYNO_PCIE_RESET_ETH;
		goto END;
	}
END:
	return ret;
}

static int syno_pcie_slot_i2c_time_check(struct pci_dev *pdev, struct i2c_adapter *pAdapter, int i2c_addr, int i2c_reg,
					 int val)
{
	int ret = -1;
	union i2c_smbus_data data;

	if (!pdev || !pAdapter) {
		pci_err(pdev, "%s Parameter error\n", __func__);
		goto END;
	}
	if (0 == val) {
		ret = 0;
		goto END;
	}
	memset(&data, 0, sizeof(data));
	ret = i2c_smbus_xfer(pAdapter, i2c_addr, 0, I2C_SMBUS_READ, i2c_reg, I2C_SMBUS_BYTE_DATA, &data);
	if (0 > ret) {
		pci_err(pdev, "%s i2c get reg:%x time failed\n", __func__, i2c_reg);
		goto END;
	}

	if (val != data.byte) {
		pci_err(pdev, "%s i2c reg:%x time: %x, got %x, need update\n", __func__, i2c_reg, val, data.byte);
		memset(&data, 0, sizeof(data));
		data.byte = val;
		ret = i2c_smbus_xfer(pAdapter, i2c_addr, 0, I2C_SMBUS_WRITE, i2c_reg, I2C_SMBUS_BYTE_DATA, &data);
		if (0 > ret) {
			pci_err(pdev, "%s i2c set time failed\n", __func__);
			goto END;
		}
	}
END:
	return ret;
}

int syno_pcie_slot_get_pwr_ctrl_info(struct pci_dev *pdev, struct syno_pci_pwr_ctrl_st *pci_pwr_ctrl)
{
	if (!pdev || !pci_pwr_ctrl) {
		if (pdev) {
			pci_err(pdev, "%s Parameter error\n", __func__);
		}
		return -1;
	}

	memset(pci_pwr_ctrl, 0, sizeof(struct syno_pci_pwr_ctrl_st));

	if (0 != syno_pci_slot_info_get(pdev, pci_pwr_ctrl) && 0 != syno_eth_slot_info_get(pdev, pci_pwr_ctrl)) {
		return -1;
	}

	return 0;
}

int syno_pcie_slot_perform_i2c_time_checks(struct pci_dev *pdev, struct i2c_adapter *pAdapter,
					   struct syno_pci_pwr_ctrl_st *pci_pwr_ctrl)
{
	int ret = -1;
	if (!pdev || !pAdapter || !pci_pwr_ctrl) {
		if (pdev) {
			pci_err(pdev, "%s Parameter error\n", __func__);
		}
		goto END;
	}

	if (0 < syno_pcie_slot_i2c_time_check(pdev, pAdapter, pci_pwr_ctrl->i2c_addr, I2C_PWR_CTL_T1_REG,
					      pci_pwr_ctrl->i2c_t1)) {
		goto END;
	}
	if (0 < syno_pcie_slot_i2c_time_check(pdev, pAdapter, pci_pwr_ctrl->i2c_addr, I2C_PWR_CTL_T3_REG,
					      pci_pwr_ctrl->i2c_t3)) {
		goto END;
	}
	if (0 < syno_pcie_slot_i2c_time_check(pdev, pAdapter, pci_pwr_ctrl->i2c_addr, I2C_PWR_CTL_T5_REG,
					      pci_pwr_ctrl->i2c_t5)) {
		goto END;
	}
	if (0 < syno_pcie_slot_i2c_time_check(pdev, pAdapter, pci_pwr_ctrl->i2c_addr, I2C_PWR_CTL_T7_REG,
					      pci_pwr_ctrl->i2c_t7)) {
		goto END;
	}
	ret = 0;

END:
	return ret;
}

int syno_pcie_slot_execute_power_action(struct i2c_adapter *pAdapter, struct syno_pci_pwr_ctrl_st *pci_pwr_ctrl)
{
	union i2c_smbus_data data;
	int i2c_delay = 0;
	int ret = -1;

	if (!pAdapter || !pci_pwr_ctrl) {
		goto END;
	}

	memset(&data, 0, sizeof(data));
	data.byte = pci_pwr_ctrl->i2c_action;
	ret = i2c_smbus_xfer(pAdapter, pci_pwr_ctrl->i2c_addr, 0, I2C_SMBUS_WRITE, pci_pwr_ctrl->i2c_register,
			     I2C_SMBUS_BYTE_DATA, &data);
	if (0 > ret) {
		goto END;
	}

	i2c_delay = pci_pwr_ctrl->i2c_delay;
	while (i2c_delay > 0) {
		if (i2c_delay > SYNO_MAX_DELAY_PERIOD) {
			mdelay(SYNO_MAX_DELAY_PERIOD);
		} else {
			mdelay(i2c_delay);
		}
		i2c_delay -= SYNO_MAX_DELAY_PERIOD;
		cond_resched();
	}

END:
	return ret;
}

int syno_pcie_slot_power_ctrl(struct pci_dev *pdev)
{
	struct syno_pci_pwr_ctrl_st pci_pwr_ctrl;
	struct i2c_adapter *pAdapter = NULL;
	int ret = -1;

	if (0 != syno_pcie_slot_get_pwr_ctrl_info(pdev, &pci_pwr_ctrl)) {
		pci_err(pdev, "No valid slot info found for power reset\n");
		goto END;
	}

	pAdapter = i2c_get_adapter(pci_pwr_ctrl.i2c_bus);
	if (NULL == pAdapter) {
		pci_err(pdev, "%s I2C initial error: failed to get i2c adapter\n", __func__);
		goto END;
	}

	if (0 != syno_pcie_slot_perform_i2c_time_checks(pdev, pAdapter, &pci_pwr_ctrl)) {
		goto END;
	}

	if (0 != syno_pcie_slot_execute_power_action(pAdapter, &pci_pwr_ctrl)) {
		pci_err(pdev, "%s PCI: Probe retry power reset failed \n", __func__);
	}

END:
	if (pAdapter) {
		i2c_put_adapter(pAdapter);
	}

	return ret;
}

struct pci_dev *syno_pci_slot_dev_get(struct pci_dev *pdev)
{
	syno_pci_reset_t type = SYNO_PCIE_RESET_NOT_SUPPORT;
	struct pci_dev *pci_slot_root_port_dev = NULL;

	if (!pdev) {
		pci_err(pdev, "%s Parameter error\n", __func__);
		goto END;
	}
	// check pcie slot reset capability
	type = syno_pci_slot_power_reset_support(pdev);

	if (SYNO_PCIE_RESET_PCIE_SLOT == type) {
		/* check subsystem id after pice slot
		 * hybrid card's ssid is not store in NIC controller
		 * we should check first device after pcie slot
		 */
		if (NULL == (pci_slot_root_port_dev = syno_pcie_slot_dev_get(pdev))) {
			pci_err(pdev, "Get pcie slot failed\n");
			goto END;
		}
	} else if (SYNO_PCIE_RESET_ETH == type) {
		// if is eth blob, assume root port is it's parent
		if (NULL == (pci_slot_root_port_dev = pdev->bus->self)) {
			pci_err(pdev, "Get eth slot failed\n");
			goto END;
		}
	}

END:
	return pci_slot_root_port_dev;
}

void syno_pci_deep_retry_find_sibling(struct pci_dev *pdev, struct list_head *pci_list_head)
{
	struct syno_pci_pwr_ctrl_st pci_pwr_ctrl;
	struct pci_dev *sibling_dev = NULL;
	char *path = NULL;
	char *token = NULL;
	struct syno_pcie_list *pcie_list_dev = NULL;
	int eth = 0;
	char szPciePath[256] = { '\0' };

	if (!pdev || !pci_list_head) {
		pci_err(pdev, "%s Parameter error\n", __func__);
		goto END;
	}

	memset(&pci_pwr_ctrl, 0, sizeof(struct syno_pci_pwr_ctrl_st));

	if (0 != syno_eth_slot_info_get(pdev, &pci_pwr_ctrl)) {
		goto END;
	}

	if ('\0' == pci_pwr_ctrl.i2c_sibling[0]) {
		goto END;
	}
	path = &pci_pwr_ctrl.i2c_sibling[0];

	while (NULL != (token = strsep(&path, ","))) {
		if (1 != sscanf(token, "eth@%d", &eth)) {
			goto END;
		}
		// get eth root_port
		if (0 != find_pcie_dev_path_by_eth(eth, szPciePath, sizeof(szPciePath))) {
			continue;
		}
		sibling_dev = syno_get_pci_dev_by_dts_pcie_root(szPciePath);
		if (sibling_dev && sibling_dev->bus->self != pdev->bus->self) {
			if (NULL == (pcie_list_dev = (struct syno_pcie_list *)kzalloc(sizeof(struct syno_pcie_list),
										      GFP_ATOMIC))) {
				continue;
			}
			pcie_list_dev->pdev = sibling_dev;
			list_add_tail(&pcie_list_dev->list, pci_list_head);
		}
	}
END:
}
bool syno_pci_power_reset_support(struct pci_dev *pdev)
{
	bool ret = false;
	struct pci_dev *pci_slot_root_port_dev = NULL;
	struct syno_pcie_list *pcie_list_dev = NULL;
	struct syno_pcie_list *tmp_dev = NULL;
	LIST_HEAD(pci_list_head);

	if (NULL == pdev) {
		goto END;
	}

	if (NULL == (pcie_list_dev = (struct syno_pcie_list *)kzalloc(sizeof(struct syno_pcie_list), GFP_ATOMIC))) {
		goto END;
	}

	pcie_list_dev->pdev = pdev;
	list_add_tail(&pcie_list_dev->list, &pci_list_head);
	syno_pci_deep_retry_find_sibling(pdev, &pci_list_head);

	pcie_list_dev = NULL;
	list_for_each_entry(pcie_list_dev, &pci_list_head, list) {
		if (NULL == (pci_slot_root_port_dev = syno_pci_slot_dev_get(pcie_list_dev->pdev))) {
			goto END;
		}

		if (false == syno_check_pci_power_reset_support(pci_slot_root_port_dev->subordinate)) {
			goto END;
		}
	}
	ret = true;
END:
	list_for_each_entry_safe(pcie_list_dev, tmp_dev, &pci_list_head, list) {
		list_del(&pcie_list_dev->list);
		kfree(pcie_list_dev);
	}
	return ret;
}

int syno_collect_pci_devices(struct pci_dev *pdev, void *data)
{
	struct list_head *dev_list = (struct list_head *)data;
	struct syno_pcie_list *entry;
	int ret = -1;

	if (!pdev) {
		pci_err(pdev, "%s Parameter error\n", __func__);
		goto END;
	}

	if (pdev->is_virtfn) {
		ret = 0;
		goto END;
	}

	entry = kmalloc(sizeof(struct syno_pcie_list), GFP_KERNEL);
	if (!entry) {
		goto END;
	}

	entry->pdev = pdev;
	list_add_tail(&entry->list, dev_list);
	ret = 0;

END:
	return ret;
}