/* Copyright (c) 2000-2024 Synology Inc. All rights reserved. */
#include <linux/synolib.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/delay.h>

struct pci_dev *syno_root_port_get(struct pci_dev *pdev)
{
	struct pci_dev *ret = NULL;
	struct pci_dev *tmp_dev = NULL;

	while (NULL != pdev) {
		if (NULL == pdev->bus) {
			goto END;
		}

		if (NULL == (tmp_dev = pci_upstream_bridge(pdev))) {
			ret = pdev;
		}
		pdev = tmp_dev;
	}
END:
	return ret;
}

struct pci_dev *syno_get_pci_dev_by_dts_pcie_root(char *szPcieRoot)
{
	struct pci_dev *pci_dev = NULL;
	struct pci_dev *root_pci_dev = NULL;
	struct pci_dev *pci_dev_ret = NULL;
	struct pci_dev *parent = NULL;
	char *path_copy = NULL;
	char *path = NULL;
	char *token = NULL;
	int domain = 0;
	int bus = 0;
	int dev = 0;
	int func = 0;

	if (NULL == szPcieRoot) {
		printk(KERN_ERR "%s: Parameter error\n", __FUNCTION__);
	}
	path_copy = kstrdup(szPcieRoot, GFP_ATOMIC);
	if (NULL == path_copy) {
		printk(KERN_ERR "%s: Failed to allocate memory\n", __FUNCTION__);
		goto END;
	}

	path = path_copy;

	// Get root port
	token = strsep(&path, ",");
	if (NULL == token) {
		printk(KERN_ERR "%s: Invalid PCI hierarchy format\n", __FUNCTION__);
		goto END;
	}

#ifdef CONFIG_SYNO_PCI_DOMAIN_PATH
	if (4 != sscanf(token, "%04x:%02x:%02x.%x", &domain, &bus, &dev, &func)) {
#else /* CONFIG_SYNO_PCI_DOMAIN_PATH */
	if (3 != sscanf(token, "%02x:%02x.%x", &bus, &dev, &func)) {
#endif /* CONFIG_SYNO_PCI_DOMAIN_PATH */
		printk(KERN_ERR "%s: Invalid PCI hierarchy format\n", __FUNCTION__);
		goto END;
	}

#ifdef CONFIG_SYNO_PCI_DOMAIN_PATH
	root_pci_dev = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(dev, func));
#else /* CONFIG_SYNO_PCI_DOMAIN_PATH */
	root_pci_dev = pci_get_domain_bus_and_slot(0, bus, PCI_DEVFN(dev, func));
#endif /* CONFIG_SYNO_PCI_DOMAIN_PATH */
	if (NULL == root_pci_dev) {
		printk(KERN_ERR "%s: Root PCI device not found: %s\n", __FUNCTION__, token);
		goto END;
	}

	parent = root_pci_dev;
	while (NULL != (token = strsep(&path, ","))) {
		if (2 != sscanf(token, "%02x.%x", &dev, &func)) {
			printk(KERN_ERR "%s: Invalid PCI hierarchy format\n", __FUNCTION__);
			goto END;
		}

		list_for_each_entry(pci_dev, &parent->subordinate->devices, bus_list) {
			if (dev == PCI_SLOT(pci_dev->devfn) && func == PCI_FUNC(pci_dev->devfn)) {
				parent = pci_dev;
				break;
			}
		}

		if (NULL == pci_dev) {
			printk(KERN_ERR "%s: PCI device %02x.%x not found under parent %s\n", __FUNCTION__, dev, func, pci_name(parent));
			goto END;
		}
	}
	pci_dev_ret = pci_dev;

END:
	if (path_copy) {
		kfree(path_copy);
	}
	if (root_pci_dev) {
		pci_dev_put(root_pci_dev);
	}
	return pci_dev_ret;
}
