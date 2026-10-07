/* Copyright (c) 2000-2024 Synology Inc. All rights reserved. */
#include <linux/synolib.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include "syno_pcie_power_ctrl.h"

static int syno_deep_retry_step_1(struct pci_dev *pdev)
{
	struct pci_driver *drv = pdev->driver;
	int ret = -1;

	if (!pdev) {
		pci_err(pdev, "%s Parameter error\n", __func__);
		goto END;
	}
	if (pdev->is_virtfn)
		return 0;

	pci_err(pdev, "Try to do syno_deep_retry_step_1\n");
	if (drv && drv->syno_deep_retry_step_1) {
		if (0 < (ret = drv->syno_deep_retry_step_1(pdev))) {
			pci_err(pdev, "Deep retry phase 1 failed\n");
			goto END;
		}
	}
	ret = 0;
END:
	return ret;
}

static int syno_deep_retry_step_2(struct pci_dev *pdev, bool release_only)
{
	struct pci_driver *drv = pdev->driver;
	int ret = -1;

	if (!pdev) {
		pci_err(pdev, "%s Parameter error\n", __func__);
		goto END;
	}
	if (pdev->is_virtfn)
		return 0;

	pci_err(pdev, "Try to do syno_deep_retry_step_2\n");
	if (drv && drv->syno_deep_retry_step_2) {
		if (0 < (ret = drv->syno_deep_retry_step_2(pdev, release_only))) {
			pci_err(pdev, "Deep retry phase 2 failed\n");
			goto END;
		}
	}
	ret = 0;
END:
	return ret;
}

void syno_pci_deep_retry(struct pci_dev *pdev)
{
	struct pci_dev *pci_slot_root_port_dev = NULL;
	struct pci_dev *pci_root_port = NULL;

	struct syno_pcie_list *pcie_list_dev = NULL;
	struct syno_pcie_list *child_pcie_list_dev = NULL;
	struct syno_pcie_list *tmp_dev = NULL;
	u16 vendor_id = 0;
	u16 dev_id = 0;
	int count = 0;
	bool step_2_release_only = false;

	LIST_HEAD(pci_list_head);
	LIST_HEAD(child_pci_list_head);

	if (NULL == pdev) {
		goto END;
	}

	if (false == syno_pci_deep_retry_support(pdev)) {
		pci_err(pdev, "This device doesn't support deep retry\n");
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
		// for syno_skip_irq
		if (NULL == (pci_root_port = syno_root_port_get(pcie_list_dev->pdev))) {
			pci_err(pcie_list_dev->pdev, "Find root port failed\n");
			goto END;
		}

		if (NULL == (pci_slot_root_port_dev = syno_pci_slot_dev_get(pcie_list_dev->pdev))) {
			goto END;
		}

		pci_walk_bus(pci_slot_root_port_dev->subordinate, syno_collect_pci_devices, &child_pci_list_head);
		list_for_each_entry_safe(child_pcie_list_dev, tmp_dev, &child_pci_list_head, list) {
			syno_deep_retry_step_1(child_pcie_list_dev->pdev);
			list_del(&child_pcie_list_dev->list);
			kfree(child_pcie_list_dev);
		}
		atomic_set(&pci_root_port->syno_skip_irq, 1);
	}

	if (0 != syno_pcie_slot_power_ctrl(pdev)) {
		pci_err(pdev, "Power reset failed\n");
	}

	pcie_list_dev = NULL;
	list_for_each_entry(pcie_list_dev, &pci_list_head, list) {
		// for syno_skip_irq
		if (NULL == (pci_root_port = syno_root_port_get(pcie_list_dev->pdev))) {
			pci_err(pcie_list_dev->pdev, "Find root port failed\n");
			goto END;
		}

		if (NULL == (pci_slot_root_port_dev = syno_pci_slot_dev_get(pcie_list_dev->pdev))) {
			goto END;
		}
		pci_walk_bus(pci_slot_root_port_dev->subordinate, syno_collect_pci_devices, &child_pci_list_head);
		list_for_each_entry_safe(child_pcie_list_dev, tmp_dev, &child_pci_list_head, list) {
			step_2_release_only = false;
			for (count = 1; count <= SYNO_PCI_READY_TIMEOUT; count++) {
				pci_read_config_word(child_pcie_list_dev->pdev, PCI_VENDOR_ID, &vendor_id);
				pci_read_config_word(child_pcie_list_dev->pdev, PCI_DEVICE_ID, &dev_id);
				if (0xffff != vendor_id && 0xffff != dev_id) {
					break;
				}

				mdelay(1000);
				cond_resched();

				if (0 == count % 10) {
					pci_err(child_pcie_list_dev->pdev, "Waiting for device ready %d\n", count);
				}
			}
			if (SYNO_PCI_READY_TIMEOUT < count) {
				pci_err(child_pcie_list_dev->pdev, "Device is not ready\n");
				step_2_release_only = true;
			}
			syno_deep_retry_step_2(child_pcie_list_dev->pdev, step_2_release_only);
			list_del(&child_pcie_list_dev->list);
			kfree(child_pcie_list_dev);
		}
		atomic_set(&pci_root_port->syno_skip_irq, 0);
	}

END:
	list_for_each_entry_safe(pcie_list_dev, tmp_dev, &pci_list_head, list) {
		list_del(&pcie_list_dev->list);
		kfree(pcie_list_dev);
	}
	return;
}
EXPORT_SYMBOL(syno_pci_deep_retry);

bool syno_pci_deep_retry_support(struct pci_dev *pdev)
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

		// if any device is occupied by guest os, we can't do nic deep retry
		if (true == syno_check_sriov_enable(pci_slot_root_port_dev->subordinate)) {
			goto END;
		}

		if (false == syno_check_pci_deep_retry_support_by_drv(pci_slot_root_port_dev->subordinate)) {
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
EXPORT_SYMBOL(syno_pci_deep_retry_support);
