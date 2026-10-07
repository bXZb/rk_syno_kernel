/* Copyright (c) 2000-2024 Synology Inc. All rights reserved. */
#include <linux/synobios.h>
#include <linux/types.h>
#include <linux/pci.h>

struct syno_pci_device_tlb {
	unsigned short vendor;
	unsigned short device;
};

static struct syno_pci_device_tlb nic_support_subsystem_tlb[] = {
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E10G18T1 },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E10G18T2 },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E10G21F2 },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E25G21F2 },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E10G30T1 },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E10G22T1_MINI },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E10G30T2_BROADCOM },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E10G22T1_MINI_AQC107 },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E10G30F2 },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E25G30F2 },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E10G30T1_AQC113 },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_ONBOARD_AQC113 },
	{ PCI_VENDOR_SYNOLOGY, PCI_DEVICE_E10G22T1_MINI_AQC113 },
	{ PCI_VENDOR_BROADCOM, PCI_DEVICE_P2100G }
};

bool syno_check_sriov_enable(struct pci_bus *bus)
{
	bool ret = false;
	struct pci_dev *pdev = NULL;

	if (NULL == bus) {
		goto END;
	}

	list_for_each_entry(pdev, &bus->devices, bus_list) {
		if (pdev->driver && 0 == strcmp(pdev->driver->name, "vfio-pci")) {
			ret = true;
			goto END;
		}
	}
END:
	return ret;
}

bool syno_check_pci_deep_retry_support_by_drv(struct pci_bus *bus)
{
	bool ret = true;
	struct pci_dev *pdev = NULL;

	if (NULL == bus) {
		goto END;
	}

	list_for_each_entry(pdev, &bus->devices, bus_list) {
		if (pdev->driver && pdev->driver->syno_deep_retry_support) {
			if (false == pdev->driver->syno_deep_retry_support(pdev)) {
				ret = false;
				goto END;
			}
		}
	}
END:
	return ret;
}

bool syno_check_pci_power_reset_support(struct pci_bus *bus)
{
	int i = 0;
	bool ret = false;
	struct pci_dev *pdev = NULL;

	if (NULL == bus) {
		goto END;
	}
	list_for_each_entry(pdev, &bus->devices, bus_list) {
		// check pci subsystem vendor/device
		for (i = 0; i < sizeof(nic_support_subsystem_tlb) / sizeof(struct syno_pci_device_tlb); i++) {
			if (pdev->subsystem_vendor == nic_support_subsystem_tlb[i].vendor &&
			    pdev->subsystem_device == nic_support_subsystem_tlb[i].device) {
				ret = true;
				goto END;
			}
		}
	}
END:
	return ret;
}
