/* Copyright (c) 2000-2024 Synology Inc. All rights reserved. */
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/synolib.h>
#include "syno_pcie_power_ctrl.h"

static LIST_HEAD(syno_probe_failed_devices);
static DEFINE_MUTEX(syno_probe_failed_mutex);

void syno_pci_probe_failed_retry(struct pci_dev *pdev)
{
	struct pci_dev *pci_slot_root_port_dev = NULL;
	struct pci_dev *root_port = NULL;
	struct syno_pci_pwr_ctrl_st pci_pwr_ctrl;
	struct syno_pcie_list *child_pcie_list_dev = NULL;
	struct syno_pcie_list *tmp_dev = NULL;
	struct i2c_adapter *pAdapter = NULL;
	char dev_name[32];

	LIST_HEAD(child_pci_list_head);

	if (!pdev) {
		goto END;
	}

	snprintf(dev_name, sizeof(dev_name), "%s", pci_name(pdev));

	if (!syno_pci_power_reset_support(pdev)) {
		pci_err(pdev, "This device doesn't support power reset\n");
		goto END;
	}

	if (0 != syno_pcie_slot_get_pwr_ctrl_info(pdev, &pci_pwr_ctrl)) {
		pci_err(pdev, "No valid slot info found for power reset\n");
		goto END;
	}

	root_port = syno_root_port_get(pdev);
	if (!root_port) {
		pci_err(pdev, "No valid root port found for retry\n");
		goto END;
	}

	pAdapter = i2c_get_adapter(pci_pwr_ctrl.i2c_bus);
	if (NULL == pAdapter) {
		pci_err(pdev, "Warning - I2C initial error: failed to get i2c adapter\n");
		goto END;
	}

	if (0 != syno_pcie_slot_perform_i2c_time_checks(pdev, pAdapter, &pci_pwr_ctrl)) {
		pci_err(pdev, "Warning - Failed to ensure correct I2C timing\n");
		goto END;
	}

	if (NULL == (pci_slot_root_port_dev = syno_pci_slot_dev_get(pdev))) {
		pci_err(pdev, "Get pcie slot failed\n");
		goto END;
	}

	atomic_set(&root_port->syno_skip_irq, 1);

	pci_walk_bus(pci_slot_root_port_dev->subordinate, syno_collect_pci_devices, &child_pci_list_head);
	list_for_each_entry_safe(child_pcie_list_dev, tmp_dev, &child_pci_list_head, list) {
		pci_stop_and_remove_bus_device_locked(child_pcie_list_dev->pdev);
		list_del(&child_pcie_list_dev->list);
		kfree(child_pcie_list_dev);
	}

	if (0 != syno_pcie_slot_execute_power_action(pAdapter, &pci_pwr_ctrl)) {
		pr_err("PCI: Probe retry power reset failed for device %s\n", dev_name);
	}

	pci_lock_rescan_remove();
	pci_rescan_bus(pci_slot_root_port_dev->bus);
	pci_unlock_rescan_remove();

	atomic_set(&root_port->syno_skip_irq, 0);

END:
	if (pAdapter) {
		i2c_put_adapter(pAdapter);
	}
	return;
}

void syno_pci_process_all_failed_devices(void)
{
	struct syno_pcie_list *entry, *tmp;
	LIST_HEAD(retry_list);

	mutex_lock(&syno_probe_failed_mutex);
	list_splice_init(&syno_probe_failed_devices, &retry_list);
	mutex_unlock(&syno_probe_failed_mutex);

	if (list_empty(&retry_list)) {
		return;
	}

	list_for_each_entry_safe(entry, tmp, &retry_list, list) {
		struct pci_dev *pdev = entry->pdev;
		pci_info(pdev, "Processing failed device %s \n", pci_name(pdev));
		syno_pci_probe_failed_retry(pdev);
		list_del(&entry->list);
		kfree(entry);
	}
	mutex_lock(&syno_probe_failed_mutex);
	list_for_each_entry_safe(entry, tmp, &syno_probe_failed_devices, list) {
		pci_dev_put(entry->pdev);
		list_del(&entry->list);
		kfree(entry);
	}
	mutex_unlock(&syno_probe_failed_mutex);
}

int syno_pci_add_probe_failed_device(struct pci_dev *pdev)
{
	int ret = 0;
	struct syno_pcie_list *entry, *existing_entry;

	if (!pdev) {
		pr_err("syno_pci_retry: Invalid device parameter\n");
		return -EINVAL;
	}

	mutex_lock(&syno_probe_failed_mutex);
	list_for_each_entry(existing_entry, &syno_probe_failed_devices, list) {
		if (existing_entry->pdev == pdev) {
			goto unlock;
		}
	}
	entry = kzalloc(sizeof(struct syno_pcie_list), GFP_KERNEL);
	if (!entry) {
		ret = -ENOMEM;
		goto unlock;
	}
	entry->pdev = pci_dev_get(pdev);
	list_add_tail(&entry->list, &syno_probe_failed_devices);
	pci_info(pdev, "Added to probe retry queue (device: %s)\n", pci_name(pdev));

unlock:
	mutex_unlock(&syno_probe_failed_mutex);
	return ret;
}
EXPORT_SYMBOL(syno_pci_add_probe_failed_device);
