#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/synolib.h>
#include <linux/of.h>
/**
 * syno_pciepath_dts_pattern_get - save pciepath of pdev into szPciePath which is a char array with length "size"
 *                                 format root_bus:device.function,device.function,...
 *                                 only keep root_bus, because bus of child layer may change
 * @pdev [IN]- the pcie device
 * @szPciePath [IN/OUT]- the char array for saving pciepath. If failed, array content is cleared.
 * @size [IN]- the length of szPciePath
 *
 * return 0: success
 *       -1: failed
 */
int syno_pciepath_dts_pattern_get(struct pci_dev *pdev, char *szPciePath, const int size)
{
	int ret = -1;
	struct pci_dev *pDevUpstream = NULL;
	char szTmp[SYNO_DTS_PROPERTY_CONTENT_LENGTH]={0};
	if (NULL == pdev || NULL == szPciePath || 0 >= size || SYNO_DTS_PROPERTY_CONTENT_LENGTH < size) {
		goto END;
	}

	while (NULL != pdev) {
		if (NULL == pdev->bus) {
			goto END;
		}

		pDevUpstream = pci_upstream_bridge(pdev);
		if (NULL == pDevUpstream) {
			/* pdev is pcie root */
			if (0 == *szPciePath) {
#ifdef CONFIG_SYNO_PCI_DOMAIN_PATH
				snprintf(szPciePath, size, "%04x:%02x:%02x.%x", pci_domain_nr(pdev->bus),
						pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
#else /* CONFIG_SYNO_PCI_DOMAIN_PATH */
				snprintf(szPciePath, size, "%02x:%02x.%x", pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
#endif /* CONFIG_SYNO_PCI_DOMAIN_PATH */
			} else {
				/* Concatenate child pcie function and device */
				strncpy(szTmp, szPciePath, size);
#ifdef CONFIG_SYNO_PCI_DOMAIN_PATH
				snprintf(szPciePath, size, "%04x:%02x:%02x.%x,%s", pci_domain_nr(pdev->bus),
						pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7, szTmp);
#else /* CONFIG_SYNO_PCI_DOMAIN_PATH */
				snprintf(szPciePath, size, "%02x:%02x.%x,%s", pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7, szTmp);
#endif /* CONFIG_SYNO_PCI_DOMAIN_PATH */
			}
			break;
		}

		if (0 == *szPciePath) {
			snprintf(szPciePath, size, "%02x.%x", (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
		} else {
			strncpy(szTmp, szPciePath, size);
			snprintf(szPciePath, size, "%02x.%x,%s", (pdev->devfn) >> 3, (pdev->devfn) & 0x7, szTmp);
		}

		pdev = pDevUpstream;
	}
	ret = 0;
END:
	if (-1 == ret) {
		memset(szPciePath, 0, size);
	}
	return ret;
}
EXPORT_SYMBOL(syno_pciepath_dts_pattern_get);

#ifdef CONFIG_SYNO_OF
static int _syno_compare_dts_pciepath(struct pci_dev *pdev, const struct device_node *pDeviceNode, bool partialCompare)
{
	int ret = -1;
	char szDevPciePath[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
	char *szDtsNodePciePath = NULL;
	size_t compareLength = SYNO_DTS_PROPERTY_CONTENT_LENGTH;

	szDtsNodePciePath = (char *)of_get_property(pDeviceNode, DT_PCIE_ROOT, NULL);

	if (NULL == szDtsNodePciePath) {
		pci_err(pdev, "%s: Read pcie_root from dts error at %s\n", __func__, of_node_full_name(pDeviceNode));
		goto END;
	}
	if (-1 == syno_pciepath_dts_pattern_get(pdev, szDevPciePath, SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		goto END;
	}
	if (partialCompare)
		compareLength = strlen(szDtsNodePciePath);
	if (0 == strncmp(szDtsNodePciePath, szDevPciePath, compareLength)) {
		ret = 0;
	}
END:
	return ret;
}

int syno_compare_dts_pciepath(struct pci_dev *pdev, const struct device_node *pDeviceNode)
{
	return _syno_compare_dts_pciepath(pdev, pDeviceNode, false);
}
EXPORT_SYMBOL(syno_compare_dts_pciepath);

int syno_compare_dts_pciepath_partial(struct pci_dev *pdev, const struct device_node *pDeviceNode)
{
	return _syno_compare_dts_pciepath(pdev, pDeviceNode, true);
}
EXPORT_SYMBOL(syno_compare_dts_pciepath_partial);

#ifdef CONFIG_SYNO_PCI_DEEP_RETRY
struct pci_dev *_syno_pcie_slot_dev_get(struct pci_dev *pdev, const struct device_node *pDeviceNode)
{
	struct pci_dev *root_pdev = NULL;
	char szDevPciePath[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
	char *szDtsNodePciePath = NULL;
	int lv = 0;
	char *ptr = NULL;

	szDtsNodePciePath = (char *)of_get_property(pDeviceNode, DT_PCIE_ROOT, NULL);

	if (NULL == szDtsNodePciePath) {
		printk(KERN_ERR "%s: Read pcie_root from dts error\n", __func__);
		goto END;
	}
	if (-1 == syno_pciepath_dts_pattern_get(pdev, szDevPciePath, SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		goto END;
	}
	if (NULL == (ptr = strstr(szDevPciePath, szDtsNodePciePath))) {
		goto END;
	}

	ptr += strlen(szDtsNodePciePath);

	while (*ptr != '\0') {
		if (*ptr == ',')
			lv++;
		ptr++;
	}

	root_pdev = pdev;
	while (0 < lv && root_pdev) {
		root_pdev = root_pdev->bus->self;
		lv--;
	}

END:
	return root_pdev;
}

struct pci_dev *syno_pcie_slot_dev_get(struct pci_dev *pdev)
{
	struct device_node *pcie_slot_node = NULL;
	struct pci_dev *root_pdev = NULL;

	if (!pdev) {
		goto END;
	}

	for_each_child_of_node(of_root, pcie_slot_node) {
		if (pcie_slot_node->full_name && 0 == strncmp(pcie_slot_node->full_name, DT_PCIE_SLOT, strlen(DT_PCIE_SLOT))) {
			if (NULL != (root_pdev = _syno_pcie_slot_dev_get(pdev, pcie_slot_node))) {
				break;
			}
		}
	}
END:
	return root_pdev;
}
EXPORT_SYMBOL(syno_pcie_slot_dev_get);
#endif /* CONFIG_SYNO_PCI_DEEP_RETRY */

#ifdef CONFIG_SYNO_PCI_EUNIT_SUPPORT
extern struct device_node *syno_pcie_path_to_eunit_root_port(const char *pciepath, bool exactly);
int syno_compare_dts_eunit_pciepath(struct pci_dev *pdev, const struct device_node *pDeviceNode)
{
	int ret = -1;
	char szDevPciePath[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
	char *szDtsNodePciePath = NULL;
	int offset = -1;
	szDtsNodePciePath = (char *)of_get_property(pDeviceNode, DT_PCIE_ROOT, NULL);

	if (NULL == szDtsNodePciePath) {
		pci_err(pdev, "%s: Read pcie_root from dts error at %s\n", __func__, of_node_full_name(pDeviceNode));
		goto END;
	}
	if (-1 == syno_pciepath_dts_pattern_get(pdev, szDevPciePath, SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		goto END;
	}
	if (NULL == syno_pcie_path_to_eunit_root_port(szDevPciePath, false)) {
		goto END;
	}
	offset=strlen(szDevPciePath)-strlen(szDtsNodePciePath);
	if (0 > offset) {
		goto END;
	}
	if (0 == strncmp(szDtsNodePciePath, szDevPciePath+offset, SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		ret = 0;
	}
END:
	return ret;
}
EXPORT_SYMBOL(syno_compare_dts_eunit_pciepath);
#endif /* CONFIG_SYNO_PCI_EUNIT_SUPPORT */
#endif /* CONFIG_SYNO_OF */
