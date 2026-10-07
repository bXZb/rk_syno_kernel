#include <linux/synolib.h>
#include <linux/of.h>
#include <linux/syno_fdt.h>
#include <linux/device.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/synobios.h>

#ifdef CONFIG_ACPI
#include <linux/platform_device.h>
#endif /* CONFIG_ACPI */

extern int syno_compare_dts_pciepath(const struct pci_dev *pdev, const struct device_node *pDeviceNode);

#ifdef CONFIG_SYNO_HWMON_PMBUS
int syno_pmbus_property_get(unsigned int *pmbus_property, const char *property_name, int index)
{
    int iRet = -1;
	if (NULL == pmbus_property || NULL == property_name) {
		goto END;
	}

    // if property name not exist, do nothing but return 0
    if (of_find_property(of_root, property_name, NULL)) {
        of_property_read_u32_index(of_root, property_name, index, pmbus_property);
    }

	iRet = 0;
END:
	return iRet;
}
EXPORT_SYMBOL(syno_pmbus_property_get);
#endif /* CONFIG_SYNO_HWMON_PMBUS */

int syno_of_i2c_mux_bus_match(int parent_bus, int parent_addr, int channel, int* index)
{
	struct device_node *pI2CNode = NULL;
	struct device_node *pI2CMuxNode = NULL;
	int iRet = -1;
	bool iMatch = false;
	int iBus = 0;
	u32 iParentBus = 0;
	char *pParentAddr = NULL;
	u32 iParentAddr = 0;
	u32 iChannel = 0;

	if(NULL == of_root || 0 > parent_bus || 0 > parent_addr || 0 > channel || NULL == index) {
		goto END;
	}

	for_each_child_of_node(of_root, pI2CNode) {
		if (!pI2CNode->full_name || 1 != sscanf(pI2CNode->full_name, DT_I2C_BUS"@%d", &iBus)) {
			continue;
		}

		for_each_child_of_node(pI2CNode, pI2CMuxNode) {
			if (!pI2CMuxNode->name || strcmp(DT_I2C_MUX, pI2CMuxNode->name)) {
				continue;
			}
			if (0 != of_property_read_u32_index(pI2CMuxNode, DT_PARENT_BUS, 0, &iParentBus)) {
				continue;
			}
			if (0 != of_property_read_u32_index(pI2CMuxNode, DT_CHANNEL_ID, 0, &iChannel)) {
				continue;
			}

			pParentAddr = (char *)of_get_property(pI2CMuxNode, DT_PARENT_ADDR, NULL);
			if (NULL == pParentAddr || 0 != kstrtou32(pParentAddr, 16, &iParentAddr)) {
				continue;
			}

			if (parent_bus == iParentBus && parent_addr == iParentAddr && channel == iChannel) {
				*index = iBus;
				iMatch = true;
				goto END;
			}
		}
	}

END:
	if (iMatch) {
		iRet = 0;
	}

	return iRet;
}
EXPORT_SYMBOL_GPL(syno_of_i2c_mux_bus_match);


bool is_syno_i2c_bus_bind(void)
{
	bool bRet = false;

	if(NULL == of_root) {
		goto END;
	}

	if (of_property_read_bool(of_root, DT_SYNO_I2C_BUS_BIND)) {
		bRet = true;
		goto END;
	}

END:
	return bRet;
}
EXPORT_SYMBOL_GPL(is_syno_i2c_bus_bind);


struct device_node* syno_of_i2c_bus_match(struct device *dev, int* index)
{
	struct device_node *pI2CNode = NULL;
	struct pci_dev *pdev = NULL;
#ifdef CONFIG_ACPI
	char *i2c_hid = NULL;
	char *i2c_uid = NULL;
	struct acpi_device *acpi_dev = NULL;
#endif

	if(NULL == of_root || NULL == dev) {
		goto END;
	}

	for_each_child_of_node(of_root, pI2CNode) {
		if (pI2CNode->full_name && 1 == sscanf(pI2CNode->full_name, DT_I2C_BUS"@%d", index)) {
			if (dev_is_pci(dev->parent) && (NULL != of_get_property(pI2CNode, DT_PCIE_ROOT, NULL))) {
				pdev = to_pci_dev(dev->parent);
				if (0 == syno_compare_dts_pciepath(pdev, pI2CNode)) {
					return pI2CNode;
				}
			}
#ifdef CONFIG_ACPI
			if (is_acpi_device_node(dev->parent->fwnode)) {
				acpi_dev = ACPI_COMPANION(dev->parent);
				i2c_hid = (char *)of_get_property(pI2CNode, DT_ACPI_HID, NULL);
				i2c_uid = (char *)of_get_property(pI2CNode, DT_ACPI_UID, NULL);

				if (!i2c_hid || !i2c_uid) {
					continue;
				}

				if ( 0 == strncmp(i2c_hid, acpi_device_hid(acpi_dev), SYNO_DTS_PROPERTY_CONTENT_LENGTH) &&
						0 == strncmp(i2c_uid, acpi_dev->pnp.unique_id, SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
					return pI2CNode;
				}
			}
#endif /* CONFIG_ACPI */
		}
	}
END:
	return NULL;
}
EXPORT_SYMBOL_GPL(syno_of_i2c_bus_match);

struct device_node* syno_of_i2c_device_match(struct i2c_client *client, const char *i2c_dev_name, struct device_node *pI2CNode)
{
	struct device_node *pI2CDevNode = NULL;
	char *device_name = NULL;
	char *device_address = NULL;
	unsigned short addr = 0;

	if (NULL == client || NULL == i2c_dev_name || NULL == pI2CNode) {
		goto END;
	}

	for_each_child_of_node(pI2CNode, pI2CDevNode) {
		device_name = (char *)of_get_property(pI2CDevNode, DT_I2C_DEVICE_NAME, NULL);
		device_address = (char *)of_get_property(pI2CDevNode, DT_I2C_ADDRESS, NULL);
		if (NULL != device_address && 0 == kstrtou16(device_address, 16, &addr)) {
			if ( 0 == strncmp(i2c_dev_name, device_name, SYNO_DTS_PROPERTY_CONTENT_LENGTH)
					&& client->addr == addr) {
				return pI2CDevNode;
			}
		}
	}

END:
	return NULL;
}
EXPORT_SYMBOL_GPL(syno_of_i2c_device_match);

struct device_node* syno_of_i2c_adapter_match(struct i2c_adapter *adap)
{
	struct device_node *pI2CNode = NULL;
	struct device_node *pRet = NULL;
	char szName[32] = {'\0'};
	int id;

	if (!adap || !of_root) {
		goto END;
	}

	id = i2c_adapter_id(adap);
	snprintf(szName, sizeof(szName), DT_I2C_BUS"@%d", id);

	for_each_child_of_node(of_root, pI2CNode) {
		if (!pI2CNode->full_name) {
			continue;
		}
		if (0 == strncmp(pI2CNode->full_name, szName, strlen(szName))) {
			pRet = pI2CNode;
			goto END;
		}
	}
END:
	return pRet;
}
EXPORT_SYMBOL_GPL(syno_of_i2c_adapter_match);

bool syno_of_i2c_driver_match_device(struct device *dev, const struct device_driver *drv)
{
	struct i2c_client *client = NULL;
	struct i2c_driver *driver = NULL;
	struct device_node *pNode = NULL;
	struct device_node *pDevNode = NULL;
	bool iRet = false;

	if(NULL == dev || NULL == drv) {
		goto END;
	}

	client = i2c_verify_client(dev);
	driver = to_i2c_driver(drv);

	if (NULL == client || NULL == driver) {
		goto END;
	}

	if (NULL != (pNode = syno_of_i2c_adapter_match(client->adapter)) &&
			NULL != (pDevNode = syno_of_i2c_device_match(client, driver->driver.name, pNode))) {
		iRet = true;
	}

END:
	return iRet;
}
EXPORT_SYMBOL_GPL(syno_of_i2c_driver_match_device);

struct i2c_adapter* syno_i2c_adapter_get_by_node(struct device_node *pI2CBusNode)
{
	struct i2c_adapter* adapter = NULL;
	int iBusIdx = 0;

	if (1 != sscanf(pI2CBusNode->full_name, DT_I2C_BUS"@%d", &iBusIdx)) {
		printk("synobios: cannot parse i2c bus index\n");
		goto END;
	}

	adapter = i2c_get_adapter(iBusIdx);

END:
	return adapter;
}
EXPORT_SYMBOL(syno_i2c_adapter_get_by_node);

int syno_pmp_get_ebox_node_by_unique_id(u8 synoUniqueID, u8 isRP, struct device_node **pEBoxNode)
{
	int iRet = -1;
	int i = 0;
	char szUnique[SYNO_EBOX_UNIQUE_MAX_LEN] = {0};

	for (i=0; syno_ebox_unique_mapping[i].uniqueId != 0; i++) {
		if (syno_ebox_unique_mapping[i].uniqueId == (synoUniqueID & syno_ebox_unique_mapping[i].mask)){
			
			if (isRP) {
				snprintf(szUnique, SYNO_EBOX_UNIQUE_MAX_LEN, "%s", syno_ebox_unique_mapping[i].szUnique);
			} else {
				snprintf(szUnique, SYNO_EBOX_UNIQUE_MAX_LEN, "%s", syno_ebox_unique_mapping[i].szUniqueRp);
			}

			if (NULL == ((*pEBoxNode) = of_get_child_by_name(of_root, szUnique))) {
				printk("Get node %s failed\n", szUnique);
				goto END;
			}

			iRet = 0;
			break;
		}
	}

END:
	return iRet;
}
EXPORT_SYMBOL(syno_pmp_get_ebox_node_by_unique_id);

int syno_pmp_i2c_addr_get(struct device_node *pNode, unsigned int *addr)
{
	int iRet = -1;
	phandle ph;
	struct device_node *pI2cNode = NULL;

	if (!pNode || !addr) {
		goto END;
	}

	/* Get I2c Device PH */
	if(of_property_read_u32_index(pNode, DT_I2C_DEVICE, 0, &ph)) {
		printk("Get I2c ph failed\n");
		goto END;
	}

	/* Get I2c Device Node*/
	if (NULL == (pI2cNode = of_find_node_by_phandle(ph))) {
		printk("Get I2c Node failed, ph = %u\n", ph);
		goto END;
	}

	/* Get I2c Addr */
	if (0 != of_property_read_u32_index(pI2cNode, DT_I2C_ADDRESS, 0 , addr)) {
		printk("Read i2c_addr failed\n");
		goto END;
	}

	iRet = 0;
END:
	return iRet;
}

EXPORT_SYMBOL(syno_pmp_i2c_addr_get);

/**
 * Get index after the name of a node for kernel with an @
 * @param puiIndex: destination for saving the index
 * @param szNodeName: the name of a node
 *
 * return 0: reading index success
 * return -1: reading index failed
 */
int syno_fdt_get_index(int *puiIndex, const char *szNodeName)
{
	int iRet = -1;
	int index = 0;
	char *pAtSign = NULL;
	pAtSign = strchr(szNodeName, '@');
	if (!pAtSign) {
		printk("Failed to find '@' in '%s'.\n", szNodeName);
		goto Err;
	}
	if (kstrtoint(pAtSign + 1, 10, &index)) {
		printk("Failed to read index number of '%s'.\n", szNodeName);
		goto Err;
    }
	*puiIndex = index;
	iRet = 0;
Err:
	return iRet;
}

#ifdef CONFIG_SYNO_SCSI_SPINDOWN_DISK_BEFORE_POWERLOSS
int syno_is_disk_power_loss_when_reboot(const char *unique)
{
	char *property_value = NULL;
	struct device_node *root_node = NULL;
	struct device_node *entry_node = NULL;

	if (unique == NULL) {
		printk("Invalid unique id\n");
		return -1;
	}

	if (strlen(unique) != 0) {
		for_each_child_of_node(of_root, entry_node) {
			if ((entry_node->full_name) &&
				(strcmp(entry_node->full_name, unique) == 0)) {
				root_node = entry_node;
			}
		}
		if (root_node == NULL) {
			printk("Can't find the device node for %s\n", unique);
			return -1;
		}
	} else {
		root_node = of_root;
	}

	property_value = (char *)of_get_property(
			root_node, DT_REBOOT_DISK_POWER_LOSS, NULL);
	if (property_value && strcmp(property_value, "true") == 0) {
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(syno_is_disk_power_loss_when_reboot);
#endif /* CONFIG_SYNO_SCSI_SPINDOWN_DISK_BEFORE_POWERLOSS */

int syno_of_get_int_property_recursive(struct device_node *pNode, const char *prop, u32 *out_val)
{
	struct device_node *child;
	int ret = -1;;

	if (!pNode || !prop || !out_val) {
		pr_err("%s: invalid parameter\n", __func__);
		goto END;
	}

	ret = of_property_read_u32(pNode, prop, out_val);
	if (!ret) {
		// Property found and return
		goto END;
	}

	// Property not found, search subnode
	for_each_child_of_node(pNode, child) {
		ret = syno_of_get_int_property_recursive(child, prop, out_val);
		if (!ret) {
			of_node_put(child);
			// Property found and return
			goto END;
		}
	}
END:
	return ret;
}
EXPORT_SYMBOL(syno_of_get_int_property_recursive);

#ifdef CONFIG_ACPI
struct device_node *syno_of_node_by_acpi_device(const struct platform_device *pdev)
{
	struct device_node *node = NULL;
	struct device_node *ret_node = NULL;
	acpi_handle handle;
	struct acpi_device *adev = NULL;
	const char *pdev_hid = NULL;
	const char *pdev_uid = NULL;
	const char *dts_hid = NULL;
	const char *dts_uid = NULL;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle) {
		goto END;
	}

	if (acpi_bus_get_device(handle, &adev))
	{
		goto END;
	}

	pdev_hid = acpi_device_hid(adev);
	pdev_uid = acpi_device_uid(adev);

	if (!pdev_hid) {
		dev_warn(&pdev->dev, "%s: ACPI device missing _HID\n", __func__);
		goto END;
	}

	// Go through SYNO DTS
	for_each_node_with_property(node, DT_ACPI_HID) {

		// Find match hid
		if (of_property_read_string(node, DT_ACPI_HID, &dts_hid))
			continue;
		if (strcmp(pdev_hid, dts_hid))
			continue;

		// Find match uid
		if (!pdev_uid) {
			// pdev_uid == NULL means no other matched hid in ACPI table, therefore return node directly
			ret_node = node;
			break;
		}

		if (of_property_read_string(node, DT_ACPI_UID, &dts_uid))
			continue;

		if (strcmp(pdev_uid, dts_uid))
			continue;

		ret_node = node;
		break;
	}

END:
	return ret_node;
}
EXPORT_SYMBOL(syno_of_node_by_acpi_device);
#endif /* CONFIG_ACPI */

#ifdef CONFIG_SYNO_I2C_GENERIC_RECOVERY_BY_DTS
void __iomem *syno_of_iomux_base_by_node(const struct device_node *node)
{
	const char *hid = NULL;
	const char *uid = NULL;
	u32 base = 0;
	u32 size = 0;
	void __iomem *iomux_base = NULL;

	if (!node) {
		pr_err("%s: null device_node\n", __func__);
		goto END;
	}

	if (of_property_read_string(node, DT_ACPI_HID, &hid)) {
		goto END;
	}
	(void)of_property_read_string(node, DT_ACPI_UID, &uid); // uid is optional

	if (of_property_read_u32(node, DT_I2C_RCVY_IOMUX_BASE, &base)) {
		goto END;
	}

	if (of_property_read_u32(node, DT_I2C_RCVY_IOMUX_BASE_LENG, &size)) {
		goto END;
	}

	iomux_base = ioremap(base, size);
	if (!iomux_base) {
		pr_err("%s ioremap failed\n", __func__);
		goto END;
	}

END:
	return iomux_base;
}
EXPORT_SYMBOL(syno_of_iomux_base_by_node);
#endif /* CONFIG_SYNO_I2C_GENERIC_RECOVERY_BY_DTS */

#ifdef CONFIG_SYNO_MICROP_COMMAND_V2
extern int gSynoMicropSeries;
void syno_microp_series_get(void)
{
	if (of_find_property(of_root, DT_SYNO_MICROP_SERIES, NULL)) {
		of_property_read_u32_index(of_root, DT_SYNO_MICROP_SERIES, 0, &gSynoMicropSeries);
	} else {
		gSynoMicropSeries = 1;
	}
}
EXPORT_SYMBOL(syno_microp_series_get);
#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */
