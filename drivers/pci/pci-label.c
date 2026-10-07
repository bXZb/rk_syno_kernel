// SPDX-License-Identifier: GPL-2.0
/*
 * Export the firmware instance and label associated with a PCI device to
 * sysfs
 *
 * Copyright (C) 2010 Dell Inc.
 * by Narendra K <Narendra_K@dell.com>,
 * Jordan Hargrave <Jordan_Hargrave@dell.com>
 *
 * PCI Firmware Specification Revision 3.1 section 4.6.7 (DSM for Naming a
 * PCI or PCI Express Device Under Operating Systems) defines an instance
 * number and string name. This code retrieves them and exports them to sysfs.
 * If the system firmware does not provide the ACPI _DSM (Device Specific
 * Method), then the SMBIOS type 41 instance number and string is exported to
 * sysfs.
 *
 * SMBIOS defines type 41 for onboard pci devices. This code retrieves
 * the instance number and string from the type 41 record and exports
 * it to sysfs.
 *
 * Please see https://linux.dell.com/files/biosdevname/ for more
 * information.
 */

#include <linux/dmi.h>
#include <linux/sysfs.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/nls.h>
#include <linux/acpi.h>
#include <linux/pci-acpi.h>
#ifdef CONFIG_SYNO_PCI_DTS_LABEL
#include <linux/of.h>
#include <linux/synolib.h>
#include <linux/ctype.h>
#endif /* CONFIG_SYNO_PCI_DTS_LABEL */
#include "pci.h"

#ifdef CONFIG_DMI
enum smbios_attr_enum {
	SMBIOS_ATTR_NONE = 0,
	SMBIOS_ATTR_LABEL_SHOW,
	SMBIOS_ATTR_INSTANCE_SHOW,
};

static size_t find_smbios_instance_string(struct pci_dev *pdev, char *buf,
					  enum smbios_attr_enum attribute)
{
	const struct dmi_device *dmi;
	struct dmi_dev_onboard *donboard;
	int domain_nr;
	int bus;
	int devfn;

	domain_nr = pci_domain_nr(pdev->bus);
	bus = pdev->bus->number;
	devfn = pdev->devfn;

	dmi = NULL;
	while ((dmi = dmi_find_device(DMI_DEV_TYPE_DEV_ONBOARD,
				      NULL, dmi)) != NULL) {
		donboard = dmi->device_data;
		if (donboard && donboard->segment == domain_nr &&
				donboard->bus == bus &&
				donboard->devfn == devfn) {
			if (buf) {
				if (attribute == SMBIOS_ATTR_INSTANCE_SHOW)
					return scnprintf(buf, PAGE_SIZE,
							 "%d\n",
							 donboard->instance);
				else if (attribute == SMBIOS_ATTR_LABEL_SHOW)
					return scnprintf(buf, PAGE_SIZE,
							 "%s\n",
							 dmi->name);
			}
			return strlen(dmi->name);
		}
	}
	return 0;
}

static umode_t smbios_instance_string_exist(struct kobject *kobj,
					    struct attribute *attr, int n)
{
	struct device *dev;
	struct pci_dev *pdev;

	dev = kobj_to_dev(kobj);
	pdev = to_pci_dev(dev);

	return find_smbios_instance_string(pdev, NULL, SMBIOS_ATTR_NONE) ?
					   S_IRUGO : 0;
}

static ssize_t smbioslabel_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev;
	pdev = to_pci_dev(dev);

	return find_smbios_instance_string(pdev, buf,
					   SMBIOS_ATTR_LABEL_SHOW);
}

static ssize_t smbiosinstance_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev;
	pdev = to_pci_dev(dev);

	return find_smbios_instance_string(pdev, buf,
					   SMBIOS_ATTR_INSTANCE_SHOW);
}

static struct device_attribute smbios_attr_label = {
	.attr = {.name = "label", .mode = 0444},
	.show = smbioslabel_show,
};

static struct device_attribute smbios_attr_instance = {
	.attr = {.name = "index", .mode = 0444},
	.show = smbiosinstance_show,
};

static struct attribute *smbios_attributes[] = {
	&smbios_attr_label.attr,
	&smbios_attr_instance.attr,
	NULL,
};

static const struct attribute_group smbios_attr_group = {
	.attrs = smbios_attributes,
	.is_visible = smbios_instance_string_exist,
};

static int pci_create_smbiosname_file(struct pci_dev *pdev)
{
	return sysfs_create_group(&pdev->dev.kobj, &smbios_attr_group);
}

static void pci_remove_smbiosname_file(struct pci_dev *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &smbios_attr_group);
}
#else
static inline int pci_create_smbiosname_file(struct pci_dev *pdev)
{
	return -1;
}

static inline void pci_remove_smbiosname_file(struct pci_dev *pdev)
{
}
#endif

#ifdef CONFIG_ACPI
enum acpi_attr_enum {
	ACPI_ATTR_LABEL_SHOW,
	ACPI_ATTR_INDEX_SHOW,
};

static void dsm_label_utf16s_to_utf8s(union acpi_object *obj, char *buf)
{
	int len;
	len = utf16s_to_utf8s((const wchar_t *)obj->buffer.pointer,
			      obj->buffer.length,
			      UTF16_LITTLE_ENDIAN,
			      buf, PAGE_SIZE - 1);
	buf[len] = '\n';
}

static int dsm_get_label(struct device *dev, char *buf,
			 enum acpi_attr_enum attr)
{
	acpi_handle handle;
	union acpi_object *obj, *tmp;
	int len = -1;

	handle = ACPI_HANDLE(dev);
	if (!handle)
		return -1;

	obj = acpi_evaluate_dsm(handle, &pci_acpi_dsm_guid, 0x2,
				DSM_PCI_DEVICE_NAME, NULL);
	if (!obj)
		return -1;

	tmp = obj->package.elements;
	if (obj->type == ACPI_TYPE_PACKAGE && obj->package.count == 2 &&
	    tmp[0].type == ACPI_TYPE_INTEGER &&
	    (tmp[1].type == ACPI_TYPE_STRING ||
	     tmp[1].type == ACPI_TYPE_BUFFER)) {
		/*
		 * The second string element is optional even when
		 * this _DSM is implemented; when not implemented,
		 * this entry must return a null string.
		 */
		if (attr == ACPI_ATTR_INDEX_SHOW) {
			scnprintf(buf, PAGE_SIZE, "%llu\n", tmp->integer.value);
		} else if (attr == ACPI_ATTR_LABEL_SHOW) {
			if (tmp[1].type == ACPI_TYPE_STRING)
				scnprintf(buf, PAGE_SIZE, "%s\n",
					  tmp[1].string.pointer);
			else if (tmp[1].type == ACPI_TYPE_BUFFER)
				dsm_label_utf16s_to_utf8s(tmp + 1, buf);
		}
		len = strlen(buf) > 0 ? strlen(buf) : -1;
	}

	ACPI_FREE(obj);

	return len;
}

static bool device_has_dsm(struct device *dev)
{
	acpi_handle handle;

	handle = ACPI_HANDLE(dev);
	if (!handle)
		return false;

	return !!acpi_check_dsm(handle, &pci_acpi_dsm_guid, 0x2,
				1 << DSM_PCI_DEVICE_NAME);
}

static umode_t acpi_index_string_exist(struct kobject *kobj,
				       struct attribute *attr, int n)
{
	struct device *dev;

	dev = kobj_to_dev(kobj);

	if (device_has_dsm(dev))
		return S_IRUGO;

	return 0;
}

static ssize_t acpilabel_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return dsm_get_label(dev, buf, ACPI_ATTR_LABEL_SHOW);
}

static ssize_t acpiindex_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return dsm_get_label(dev, buf, ACPI_ATTR_INDEX_SHOW);
}

static struct device_attribute acpi_attr_label = {
	.attr = {.name = "label", .mode = 0444},
	.show = acpilabel_show,
};

static struct device_attribute acpi_attr_index = {
	.attr = {.name = "acpi_index", .mode = 0444},
	.show = acpiindex_show,
};

static struct attribute *acpi_attributes[] = {
	&acpi_attr_label.attr,
	&acpi_attr_index.attr,
	NULL,
};

static const struct attribute_group acpi_attr_group = {
	.attrs = acpi_attributes,
	.is_visible = acpi_index_string_exist,
};

static int pci_create_acpi_index_label_files(struct pci_dev *pdev)
{
	return sysfs_create_group(&pdev->dev.kobj, &acpi_attr_group);
}

static int pci_remove_acpi_index_label_files(struct pci_dev *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &acpi_attr_group);
	return 0;
}
#else
static inline int pci_create_acpi_index_label_files(struct pci_dev *pdev)
{
	return -1;
}

static inline int pci_remove_acpi_index_label_files(struct pci_dev *pdev)
{
	return -1;
}

static inline bool device_has_dsm(struct device *dev)
{
	return false;
}
#endif

#ifdef CONFIG_SYNO_PCI_DTS_LABEL
enum syno_dt_attr_enum {
	SYNO_DT_ATTR_LABEL_SHOW,
	SYNO_DT_ATTR_INDEX_SHOW,
};

/* DT_LABEL, DT_NVME_SLOT, DT_BUILTIN_GPU, DT_M2_CARD, DT_NVME_NODE,
 * DT_PROTOCOL_TYPE, DT_LOCATION, DT_ONBOARD_DEVICE, DT_PCIE_ROOT
 * and syno_direct_pcie_slot_types[] are defined in <linux/synolib.h>
 */

extern int syno_pciepath_dts_pattern_get(struct pci_dev *pdev, char *szPciePath, const int size);

/**
 * syno_dt_count_trailing_pci_segments - count trailing ",XX.X" PCI devfn segments
 *
 * Counts consecutive trailing segments of the form ",XX.X" where X is a
 * hexadecimal digit. Only the last segment is expected to be ",00.0";
 * intermediate segments may have arbitrary devfn values.
 */
static int syno_dt_count_trailing_pci_segments(const char *path)
{
	int count = 0;
	size_t len = strlen(path);

	while (len >= 5 &&
	       path[len - 5] == ',' &&
	       isxdigit(path[len - 4]) &&
	       isxdigit(path[len - 3]) &&
	       path[len - 2] == '.' &&
	       isxdigit(path[len - 1])) {
		count++;
		len -= 5;
	}
	return count;
}

/**
 * syno_dt_adjust_nvme_slot_addr - strip one trailing ",00.0" if count is odd
 *
 * Some nvme_slot / system_slot+nvme DTS entries have an extra ",00.0" suffix
 * that points to the endpoint rather than the slot bridge. When the count of
 * trailing ",00.0" segments is odd, strip the last one to get the real slot
 * address.
 *
 * Returns true if adjusted, false if no adjustment needed.
 */
static bool syno_dt_adjust_nvme_slot_addr(const char *pcie_root,
					  char *out, size_t out_size)
{
	int count = syno_dt_count_trailing_pci_segments(pcie_root);
	size_t len = strlen(pcie_root);

	if (count > 0 && (count & 1) && len >= 5) {
		size_t new_len = len - 5;

		if (new_len >= out_size) {
			new_len = out_size - 1;
		}
		memcpy(out, pcie_root, new_len);
		out[new_len] = '\0';
		return true;
	}
	return false;
}

/**
 * syno_dt_slot_needs_addr_adjust - check if slot type needs nvme addr adjustment
 */
static bool syno_dt_slot_needs_addr_adjust(struct device_node *slot_node,
					   const char *slot_type)
{
	const char *proto;

	if (strcmp(slot_type, DT_NVME_SLOT) == 0) {
		return true;
	}
	if (strcmp(slot_type, DT_SYSTEM_SLOT) == 0) {
		proto = of_get_property(slot_node, DT_PROTOCOL_TYPE, NULL);
		if (proto && strcmp(proto, "nvme") == 0) {
			return true;
		}
	}
	return false;
}

/**
 * syno_dt_match_path_str - compare device PCI path against a string path
 * @devpath: device PCI path
 * @dts_path: DTS path string to match against
 * @out_match_len: (optional) output for the matched path length
 *
 * Returns: 0 for exact match, 1 for partial (prefix) match, -1 for no match
 */
static int syno_dt_match_path_str(const char *devpath, const char *dts_path,
				  size_t *out_match_len)
{
	size_t dts_len;

	if (!dts_path) {
		return -1;
	}

	dts_len = strlen(dts_path);

	if (strcmp(devpath, dts_path) == 0) {
		if (out_match_len) {
			*out_match_len = dts_len;
		}
		return 0;
	}

	if (strncmp(devpath, dts_path, dts_len) == 0 && devpath[dts_len] == ',') {
		if (out_match_len) {
			*out_match_len = dts_len;
		}
		return 1;
	}

	return -1;
}

/**
 * syno_dt_get_onboard_dev_label - get onboard device label from DTS
 *
 * Reads onboard_device@N nodes directly under of_root.
 * Matches device PCI path against pcie_root property.
 * Returns "(EP)<label>" for the matched onboard device.
 */
static int syno_dt_get_onboard_dev_label(struct pci_dev *pdev, char *buf,
				  enum syno_dt_attr_enum attr)
{
	struct device_node *node = NULL;
	char devpath[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
	const char *dts_path;
	const char *name;
	int index = 0;
	int len = -1;

	if (!of_root) {
		return -1;
	}

	if (syno_pciepath_dts_pattern_get(pdev, devpath,
					  SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		return -1;
	}

	for_each_child_of_node(of_root, node) {
		if (!node->full_name ||
		    1 != sscanf(node->full_name, DT_ONBOARD_DEVICE "@%d",
				&index)) {
			continue;
		}

		dts_path = of_get_property(node, DT_PCIE_ROOT, NULL);
		if (!dts_path) {
			continue;
		}

		if (strcmp(devpath, dts_path) == 0) {
			if (attr == SYNO_DT_ATTR_LABEL_SHOW) {
				name = of_get_property(node, DT_LABEL, NULL);
				if (name && buf) {
					len = scnprintf(buf, SYNO_PCI_LABEL_MAX_LEN,
							"(EP)%s\n", name);
				} else if (name) {
					len = strlen(name) + 5; /* "(EP)" + '\n' */
				}
			} else if (attr == SYNO_DT_ATTR_INDEX_SHOW) {
				if (buf) {
					len = scnprintf(buf, SYNO_PCI_LABEL_MAX_LEN,
							"%d\n", index);
				} else {
					len = 1;
				}
			}
			of_node_put(node);
			break;
		}
	}

	/*
	 * Multi-function fallback: if exact match failed and this is an
	 * endpoint device, try matching with function number replaced by 0.
	 * e.g., ",00.1" or ",00.2" will try to match ",00.0".
	 */
	if (len == -1 && pdev->hdr_type == PCI_HEADER_TYPE_NORMAL) {
		size_t path_len = strlen(devpath);

		if (path_len >= 5 &&
		    devpath[path_len - 5] == ',' &&
		    isxdigit(devpath[path_len - 4]) &&
		    isxdigit(devpath[path_len - 3]) &&
		    devpath[path_len - 2] == '.' &&
		    isxdigit(devpath[path_len - 1]) &&
		    devpath[path_len - 1] != '0') {
			char mf_path[SYNO_DTS_PROPERTY_CONTENT_LENGTH];

			memcpy(mf_path, devpath, path_len + 1);
			mf_path[path_len - 1] = '0';

			for_each_child_of_node(of_root, node) {
				if (!node->full_name ||
				    1 != sscanf(node->full_name,
						DT_ONBOARD_DEVICE "@%d",
						&index)) {
					continue;
				}

				dts_path = of_get_property(node,
							   DT_PCIE_ROOT, NULL);
				if (!dts_path) {
					continue;
				}

				if (strcmp(mf_path, dts_path) == 0) {
					if (attr == SYNO_DT_ATTR_LABEL_SHOW) {
						name = of_get_property(node,
								DT_LABEL, NULL);
						if (name && buf) {
							len = scnprintf(buf,
								SYNO_PCI_LABEL_MAX_LEN,
								"(EP)%s\n", name);
						} else if (name) {
							len = strlen(name) + 5;
						}
					} else if (attr == SYNO_DT_ATTR_INDEX_SHOW) {
						if (buf) {
							len = scnprintf(buf,
								SYNO_PCI_LABEL_MAX_LEN,
								"%d\n", index);
						} else {
							len = 1;
						}
					}
					of_node_put(node);
					break;
				}
			}
		}
	}

	return len;
}

/**
 * syno_dt_get_rootport_label - get (RP)<label> for bridge port of onboard device
 * @pdev: root port or downstream port pci device
 * @buf: buffer to write label into (or NULL to just check existence)
 * @attr: which attribute to retrieve
 *
 * If a direct child device of this bridge has an onboard_device DTS label,
 * this returns "(RP)<child_label>" as the label for the bridge.
 * Matching is done by stripping the last ",XX.X" segment from the
 * onboard_device's pcie_root and comparing exactly against this device's path.
 */
static int syno_dt_get_rootport_label(struct pci_dev *pdev, char *buf,
				      enum syno_dt_attr_enum attr)
{
	struct device_node *node = NULL;
	const char *dts_path;
	const char *name;
	const char *last_comma;
	char szRpPath[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
	int rpPathLen;
	int index = 0;
	int len = -1;

	if (!of_root) {
		return -1;
	}

	if (!pci_is_pcie(pdev) ||
	    (pci_pcie_type(pdev) != PCI_EXP_TYPE_ROOT_PORT &&
	     pci_pcie_type(pdev) != PCI_EXP_TYPE_DOWNSTREAM)) {
		return -1;
	}

	if (syno_pciepath_dts_pattern_get(pdev, szRpPath,
					  SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		return -1;
	}

	rpPathLen = strlen(szRpPath);
	if (rpPathLen == 0) {
		return -1;
	}

	for_each_child_of_node(of_root, node) {
		if (!node->full_name ||
		    1 != sscanf(node->full_name, DT_ONBOARD_DEVICE "@%d",
				&index)) {
			continue;
		}

		dts_path = of_get_property(node, DT_PCIE_ROOT, NULL);
		if (!dts_path) {
			continue;
		}

		/* Strip last ",XX.X" segment from dts_path to get parent path */
		last_comma = strrchr(dts_path, ',');
		if (!last_comma || strlen(last_comma) != 5 ||
		    !isxdigit(last_comma[1]) || !isxdigit(last_comma[2]) ||
		    last_comma[3] != '.' || !isxdigit(last_comma[4])) {
			continue;
		}

		/* Exact match: dts_path minus last segment == device path */
		if ((last_comma - dts_path) == rpPathLen &&
		    strncmp(dts_path, szRpPath, rpPathLen) == 0) {
			if (attr == SYNO_DT_ATTR_LABEL_SHOW) {
				name = of_get_property(node, DT_LABEL, NULL);
				if (name && buf) {
					len = scnprintf(buf, SYNO_PCI_LABEL_MAX_LEN,
							"(RP)%s\n", name);
				} else if (name) {
					len = strlen(name) + 5; /* "(RP)" + '\n' */
				}
			} else if (attr == SYNO_DT_ATTR_INDEX_SHOW) {
				if (buf) {
					len = scnprintf(buf, SYNO_PCI_LABEL_MAX_LEN,
							"%d\n", index);
				} else {
					len = 1;
				}
			}
			of_node_put(node);
			break;
		}
	}

	return len;
}

/**
 * syno_dt_match_pcie_path - compare device PCI path against a node's pcie_root
 * @devpath: the device's PCI path string
 * @node: device tree node containing pcie_root property
 * @adjust: if true, apply odd ",00.0" stripping for nvme slot addresses
 * @out_match_len: (optional) output for the matched path length
 *
 * Returns: 0 for exact match, 1 for partial (prefix) match, -1 for no match
 */
static int syno_dt_match_pcie_path(const char *devpath,
				   struct device_node *node,
				   bool adjust,
				   size_t *out_match_len)
{
	const char *dts_path;
	char adjusted[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
	const char *match_path;

	dts_path = of_get_property(node, DT_PCIE_ROOT, NULL);
	if (!dts_path) {
		return -1;
	}

	match_path = dts_path;
	if (adjust && syno_dt_adjust_nvme_slot_addr(dts_path, adjusted,
						    sizeof(adjusted))) {
		match_path = adjusted;
	}

	return syno_dt_match_path_str(devpath, match_path, out_match_len);
}

/* syno_direct_pcie_slot_types[] is declared in <linux/synolib.h> */
const char * const syno_direct_pcie_slot_types[] = {
	DT_PCIE_SLOT,        /* "pcie_slot" */
	DT_NVME_SLOT,        /* "nvme_slot" */
	DT_BUILTIN_GPU,      /* "builtin_gpu" */
	DT_PCIE_EUNIT_SLOT,  /* "pcie_eunit_slot" */
	NULL,
};

/**
 * syno_dt_is_pcie_slot - check if a DT node is a known PCIe slot type
 * @node: device tree node to check
 * @slot_type: output buffer for the slot type name
 * @type_size: size of slot_type buffer
 * @out_index: output for the slot index number
 *
 * Returns true if the node is a known PCIe slot, false otherwise.
 */
static bool syno_dt_is_pcie_slot(struct device_node *node,
				 char *slot_type, size_t type_size,
				 int *out_index)
{
	const char * const *type;
	const char *proto;
	struct device_node *nvme_child;
	int idx = 0;
	char fmt[64];

	if (!node->full_name) {
		return false;
	}

	/* Check direct slot types */
	for (type = syno_direct_pcie_slot_types; *type; type++) {
		snprintf(fmt, sizeof(fmt), "%s@%%d", *type);
		if (1 == sscanf(node->full_name, fmt, &idx)) {
			strncpy(slot_type, *type, type_size - 1);
			slot_type[type_size - 1] = '\0';
			*out_index = idx;
			return true;
		}
	}

	/* Check system_slot with protocol_type = "nvme" */
	if (1 == sscanf(node->full_name, DT_SYSTEM_SLOT "@%d", &idx)) {
		proto = of_get_property(node, DT_PROTOCOL_TYPE, NULL);
		if (proto && strcmp(proto, "nvme") == 0) {
			strncpy(slot_type, DT_SYSTEM_SLOT, type_size - 1);
			slot_type[type_size - 1] = '\0';
			*out_index = idx;
			return true;
		}
	}

	/* Check internal_slot with nvme child */
	if (1 == sscanf(node->full_name, DT_INTERNAL_SLOT "@%d", &idx)) {
		nvme_child = of_get_child_by_name(node, DT_NVME_NODE);
		if (nvme_child) {
			of_node_put(nvme_child);
			strncpy(slot_type, DT_INTERNAL_SLOT, type_size - 1);
			slot_type[type_size - 1] = '\0';
			*out_index = idx;
			return true;
		}
	}

	return false;
}

/**
 * syno_dt_match_slot_pcie_root - match a PCI device path against a slot's pcie_root(s)
 * @devpath: device PCI path
 * @slot_node: the slot's device tree node
 * @slot_type: the slot type name
 * @out_match_len: (optional) output for the matched pcie_root string length
 *
 * For internal_slot with nvme, pcie_root may be in nvme child or nvme/location@N.
 * For nvme_slot / system_slot+nvme, applies odd ",00.0" address adjustment.
 * Returns: 0 for exact, 1 for partial, -1 for no match.
 */
static int syno_dt_match_slot_pcie_root(const char *devpath,
					struct device_node *slot_node,
					const char *slot_type,
					size_t *out_match_len)
{
	struct device_node *nvme_child, *loc_child;
	bool adjust;
	int ret;

	if (strcmp(slot_type, DT_INTERNAL_SLOT) == 0) {
		nvme_child = of_get_child_by_name(slot_node, DT_NVME_NODE);
		if (!nvme_child) {
			return -1;
		}

		/* Check nvme node's own pcie_root (with adjustment) */
		ret = syno_dt_match_pcie_path(devpath, nvme_child, true,
					      out_match_len);
		if (ret >= 0) {
			of_node_put(nvme_child);
			return ret;
		}

		/* Check location@N children */
		for_each_child_of_node(nvme_child, loc_child) {
			if (!loc_child->full_name ||
			    strncmp(loc_child->full_name, DT_LOCATION,
				    strlen(DT_LOCATION))) {
				continue;
			}

			ret = syno_dt_match_pcie_path(devpath, loc_child, true,
						      out_match_len);
			if (ret >= 0) {
				of_node_put(loc_child);
				of_node_put(nvme_child);
				return ret;
			}
		}

		of_node_put(nvme_child);
		return -1;
	}

	/* For other slot types, pcie_root is directly in the node */
	adjust = syno_dt_slot_needs_addr_adjust(slot_node, slot_type);
	return syno_dt_match_pcie_path(devpath, slot_node, adjust, out_match_len);
}

/**
 * syno_dt_build_parent_label - find the parent slot for a given PCI path prefix
 * @prefix: the PCI path prefix (part before the postfix)
 * @label: output buffer for parent label (e.g., "pcie_slot#1")
 * @label_size: buffer size
 *
 * Iterates through all known PCIe slot types and finds the most specific
 * (longest pcie_root) match for the given prefix.
 * Returns: label length on success, 0 if no parent found.
 */
static int syno_dt_build_parent_label(const char *prefix,
				      char *label, size_t label_size)
{
	struct device_node *node;
	char slot_type[64];
	int index;
	int best_match_len = 0;
	char best_label[128] = {'\0'};

	if (!of_root || !prefix || !*prefix) {
		return 0;
	}

	for_each_child_of_node(of_root, node) {
		size_t root_len = 0;

		if (!syno_dt_is_pcie_slot(node, slot_type, sizeof(slot_type),
					  &index)) {
			continue;
		}

		if (syno_dt_match_slot_pcie_root(prefix, node, slot_type,
						&root_len) >= 0) {
			if (root_len > best_match_len) {
				best_match_len = root_len;
				snprintf(best_label, sizeof(best_label),
					 "%s#%d", slot_type, index);
			}
		}
	}

	if (best_label[0]) {
		strncpy(label, best_label, label_size - 1);
		label[label_size - 1] = '\0';
		return strlen(label);
	}

	return 0;
}

/**
 * syno_dt_find_switch_upstream_port - find the PCIe switch upstream port above pdev
 * @pdev: starting PCI device
 *
 * Walks up the PCI tree from pdev to find the first ancestor with
 * PCIe type PCI_EXP_TYPE_UPSTREAM.
 * Returns the upstream port pci_dev or NULL if not found.
 */
static struct pci_dev *syno_dt_find_switch_upstream_port(struct pci_dev *pdev)
{
	struct pci_dev *bridge;

	bridge = pci_upstream_bridge(pdev);
	while (bridge) {
		if (pci_is_pcie(bridge) &&
		    pci_pcie_type(bridge) == PCI_EXP_TYPE_UPSTREAM) {
			return bridge;
		}
		bridge = pci_upstream_bridge(bridge);
	}
	return NULL;
}

/**
 * syno_dt_check_card_subsystem_id - verify expansion card identity by subsystem ID
 * @pdev: PCI device being labeled (must be below the expansion card's switch)
 * @card_node: the expansion card's device tree node (e.g., E10M20-T1)
 *
 * If the card node defines pcie_subsystem_id = <vendor device>, find the
 * PCIe switch upstream port above pdev and compare its subsystem vendor/device
 * IDs against the expected values.
 *
 * Returns true if match.
 */
static bool syno_dt_check_card_subsystem_id(struct pci_dev *pdev,
					    struct device_node *card_node)
{
	const __be32 *prop;
	int prop_len;
	u16 expected_vendor, expected_device;
	struct pci_dev *upstream;

	prop = of_get_property(card_node, "pcie_subsystem_id", &prop_len);
	if (!prop || prop_len < 2 * sizeof(__be32)) {
		return false;
	}

	expected_vendor = (u16)be32_to_cpup(prop);
	expected_device = (u16)be32_to_cpup(prop + 1);

	upstream = syno_dt_find_switch_upstream_port(pdev);
	if (!upstream) {
		return false;
	}

	return (upstream->subsystem_vendor == expected_vendor &&
		upstream->subsystem_device == expected_device);
}

/**
 * syno_dt_normalize_postfix - normalize pcie_postfix to slot bridge level
 * @postfix: the raw pcie_postfix string from DTS
 * @out: output buffer for normalized postfix
 * @out_size: size of output buffer
 *
 * Expansion card pcie_postfix may include the endpoint segment or stop at
 * the slot bridge. The leading segment has no comma prefix, so we add 1 to
 * the trailing segment count from syno_dt_count_trailing_pci_segments() to
 * get the total number of segments. If the total is odd, the postfix includes
 * the endpoint — strip the last ",XX.X" segment.
 *
 * Returns true if adjusted (out contains normalized postfix).
 * Returns false if already at slot level (out is not modified; use postfix as-is).
 */
static bool syno_dt_normalize_postfix(const char *postfix,
				      char *out, size_t out_size)
{
	int trailing_count = syno_dt_count_trailing_pci_segments(postfix);
	int total_segments = trailing_count + 1;
	size_t len = strlen(postfix);

	if ((total_segments & 1) && len >= 5) {
		size_t new_len = len - 5;

		if (new_len == 0) {
			return false;
		}
		if (new_len >= out_size) {
			new_len = out_size - 1;
		}
		memcpy(out, postfix, new_len);
		out[new_len] = '\0';
		return true;
	}
	return false;
}

/**
 * syno_dt_get_slot_label - generate label from DTS slot definitions
 * @pdev: PCI device
 * @buf: output buffer (NULL to just check existence)
 * @attr: which attribute to show
 *
 * Label generation:
 *
 * Phase 1 — Expansion card matching (m2_card / onboard_device with pcie_postfix):
 *   Endpoint:  "(EP)m2_card#N@card@parent" or "(EP)<label>@card@parent"
 *   Root port: "(RP)m2_card#N@card@parent" or "(RP)<label>@card@parent"
 *
 * Phase 2 — Direct slot matching:
 *   Exact match (the slot bridge itself): "(RP)slot_type#N"
 *   Partial match (device downstream): "(EP)slot_type#N"
 *
 * Priority: expansion endpoint > slot exact > expansion DP > slot partial
 *
 * Returns: label length on success, -1 if no match.
 */
static int syno_dt_get_slot_label(struct pci_dev *pdev, char *buf,
				  enum syno_dt_attr_enum attr)
{
	char devpath[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
	struct device_node *node, *child, *nvme_child;
	char slot_type[64];
	char label[256] = {'\0'};
	int index = 0;
	int len = -1;
	int best_partial_prio = 0;
	char best_exact_label[256] = {'\0'};
	int best_exact_index = 0;
	char best_partial_label[256] = {'\0'};
	int best_partial_index = 0;
	char best_dp_label[256] = {'\0'};
	int best_dp_index = 0;

	if (!of_root) {
		return -1;
	}

	if (syno_pciepath_dts_pattern_get(pdev, devpath,
					  SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		return -1;
	}

	/*
	 * Phase 1: Check expansion card nodes for m2_card and onboard_device
	 * with pcie_postfix. Match endpoints and downstream ports (DP).
	 */
	for_each_child_of_node(of_root, node) {
		for_each_child_of_node(node, child) {
			const char *postfix = NULL;
			const char *child_label = NULL;
			int child_index = 0;
			bool is_m2_card = false;
			size_t dev_len;
			char norm_buf[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
			const char *slot_postfix;
			size_t slot_len;

			/* Check m2_card@N with nvme { pcie_postfix } */
			if (child->full_name &&
			    1 == sscanf(child->full_name, DT_M2_CARD "@%d",
					&child_index)) {
				nvme_child = of_get_child_by_name(child,
								  DT_NVME_NODE);
				if (nvme_child) {
					postfix = of_get_property(nvme_child,
						DT_PCIE_EUNIT_POSTFIX, NULL);
					of_node_put(nvme_child);
				}
				is_m2_card = true;
			}
			/* Check expansion card onboard_device@N */
			else if (child->full_name &&
				 1 == sscanf(child->full_name,
					     DT_ONBOARD_DEVICE "@%d",
					     &child_index)) {
				postfix = of_get_property(child,
						DT_PCIE_EUNIT_POSTFIX, NULL);
				child_label = of_get_property(child,
						DT_LABEL, NULL);
			}

			if (!postfix) {
				continue;
			}

			/* Normalize postfix to slot bridge level */
			if (syno_dt_normalize_postfix(postfix, norm_buf,
						      sizeof(norm_buf))) {
				slot_postfix = norm_buf;
			} else {
				slot_postfix = postfix;
			}
			slot_len = strlen(slot_postfix);
			dev_len = strlen(devpath);

			/*
			 * EP match: devpath ends with "," + slot_postfix + ",XX.X"
			 * Any device function below the slot bridge matches.
			 */
			if (dev_len > slot_len + 6) {
				const char *ep_start = devpath + dev_len - slot_len - 5;

				if (*(ep_start - 1) == ',' &&
				    strncmp(ep_start, slot_postfix, slot_len) == 0 &&
				    ep_start[slot_len] == ',' &&
				    isxdigit((unsigned char)ep_start[slot_len + 1]) &&
				    isxdigit((unsigned char)ep_start[slot_len + 2]) &&
				    ep_start[slot_len + 3] == '.' &&
				    isxdigit((unsigned char)ep_start[slot_len + 4])) {
					char prefix[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
					char parent_label[128] = {'\0'};
					const char *card_name = node->full_name;
					size_t prefix_len = (ep_start - 1) - devpath;

					/* Verify card identity by subsystem ID */
					if (!syno_dt_check_card_subsystem_id(pdev, node))
						continue;

					if (prefix_len >= sizeof(prefix)) {
						prefix_len = sizeof(prefix) - 1;
					}
					memcpy(prefix, devpath, prefix_len);
					prefix[prefix_len] = '\0';

					syno_dt_build_parent_label(prefix,
						parent_label,
						sizeof(parent_label));

					if (is_m2_card) {
						if (parent_label[0]) {
							snprintf(label, sizeof(label),
								 "(EP)%s#%d@%s@%s",
								 DT_M2_CARD, child_index,
								 card_name, parent_label);
						} else {
							snprintf(label, sizeof(label),
								 "(EP)%s#%d@%s",
								 DT_M2_CARD, child_index,
								 card_name);
						}
					} else if (child_label) {
						if (parent_label[0]) {
							snprintf(label, sizeof(label),
								 "(EP)%s@%s@%s",
								 child_label, card_name,
								 parent_label);
						} else {
							snprintf(label, sizeof(label),
								 "(EP)%s@%s",
								 child_label, card_name);
						}
					}

					index = child_index;
					of_node_put(child);
					of_node_put(node);
					goto found;
				}
			}

			/*
			 * DP match: devpath ends with "," + slot_postfix
			 * The device IS the slot bridge (downstream port).
			 */
			if (dev_len > slot_len + 1) {
				const char *dp_start = devpath + dev_len - slot_len;

				if (*(dp_start - 1) == ',' &&
				    strcmp(dp_start, slot_postfix) == 0) {
					char prefix[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
					char parent_label[128] = {'\0'};
					const char *card_name = node->full_name;
					size_t prefix_len = (dp_start - 1) - devpath;

					/* Verify card identity by subsystem ID */
					if (!syno_dt_check_card_subsystem_id(pdev, node))
						continue;

					if (prefix_len >= sizeof(prefix)) {
						prefix_len = sizeof(prefix) - 1;
					}
					memcpy(prefix, devpath, prefix_len);
					prefix[prefix_len] = '\0';

					syno_dt_build_parent_label(prefix,
						parent_label,
						sizeof(parent_label));

					if (is_m2_card) {
						if (parent_label[0]) {
							snprintf(best_dp_label,
								 sizeof(best_dp_label),
								 "(RP)%s#%d@%s@%s",
								 DT_M2_CARD, child_index,
								 card_name, parent_label);
						} else {
							snprintf(best_dp_label,
								 sizeof(best_dp_label),
								 "(RP)%s#%d@%s",
								 DT_M2_CARD, child_index,
								 card_name);
						}
					} else if (child_label) {
						if (parent_label[0]) {
							snprintf(best_dp_label,
								 sizeof(best_dp_label),
								 "(RP)%s@%s@%s",
								 child_label, card_name,
								 parent_label);
						} else {
							snprintf(best_dp_label,
								 sizeof(best_dp_label),
								 "(RP)%s@%s",
								 child_label, card_name);
						}
					}
					best_dp_index = child_index;
				}
			}
		}
	}

	/*
	 * Phase 2: Direct slot matching
	 * Exact match produces "(RP)slot_type#N", partial produces "(EP)slot_type#N".
	 */
	for_each_child_of_node(of_root, node) {
		int ret;
		size_t root_len = 0;

		if (!syno_dt_is_pcie_slot(node, slot_type, sizeof(slot_type),
					  &index)) {
			continue;
		}

		ret = syno_dt_match_slot_pcie_root(devpath, node, slot_type,
						   &root_len);
		if (ret == 0) {
			/* Exact match - this is the slot bridge itself */
			snprintf(best_exact_label, sizeof(best_exact_label),
				 "(RP)%s#%d", slot_type, index);
			best_exact_index = index;
			of_node_put(node);
			break;
		} else if (ret == 1) {
			/* Partial match - device is downstream of this slot */
			if (root_len > best_partial_prio) {
				best_partial_prio = root_len;
				snprintf(best_partial_label,
					 sizeof(best_partial_label),
					 "(EP)%s#%d", slot_type, index);
				best_partial_index = index;
			}
		}
	}

	/*
	 * Priority: slot exact > expansion DP > slot partial
	 * (expansion endpoint already returned via goto above)
	 */
	if (best_exact_label[0]) {
		strncpy(label, best_exact_label, sizeof(label) - 1);
		label[sizeof(label) - 1] = '\0';
		index = best_exact_index;
		goto found;
	}

	if (best_dp_label[0]) {
		strncpy(label, best_dp_label, sizeof(label) - 1);
		label[sizeof(label) - 1] = '\0';
		index = best_dp_index;
		goto found;
	}

	if (best_partial_label[0]) {
		strncpy(label, best_partial_label, sizeof(label) - 1);
		label[sizeof(label) - 1] = '\0';
		index = best_partial_index;
		goto found;
	}

	return -1;

found:
	if (attr == SYNO_DT_ATTR_LABEL_SHOW) {
		if (buf) {
			len = scnprintf(buf, SYNO_PCI_LABEL_MAX_LEN, "%s\n", label);
		} else {
			len = strlen(label) + 1; /* '\n' */
		}
	} else if (attr == SYNO_DT_ATTR_INDEX_SHOW) {
		if (buf) {
			len = scnprintf(buf, SYNO_PCI_LABEL_MAX_LEN, "%d\n", index);
		} else {
			len = 1;
		}
	}

	return len;
}

/**
 * syno_dt_get_switch_dp_inherited_label - inherit label from switch upstream port
 * @pdev: PCIe switch downstream port device
 * @buf: buffer for label (or NULL to check existence)
 * @attr: which attribute to show
 *
 * If the device is a PCIe switch downstream port with no direct label,
 * find the upstream port of the switch and use its onboard_device label.
 *
 * Returns: label length on success, -1 if not applicable or no label found.
 */
static int syno_dt_get_switch_dp_inherited_label(struct pci_dev *pdev, char *buf,
						 enum syno_dt_attr_enum attr)
{
	struct pci_dev *upstream;

	if (!pci_is_pcie(pdev) ||
	    pci_pcie_type(pdev) != PCI_EXP_TYPE_DOWNSTREAM) {
		return -1;
	}

	upstream = syno_dt_find_switch_upstream_port(pdev);
	if (!upstream) {
		return -1;
	}

	return syno_dt_get_onboard_dev_label(upstream, buf, attr);
}

/**
 * syno_dt_get_label - get Synology DTS label for a PCI device (internal)
 * @pdev: the PCI device
 * @buf: buffer for label output (or NULL to just check existence)
 * @attr: which attribute to show (label or index)
 *
 * Walks the priority chain:
 *   1. onboard_dev_label (exact + multi-function)
 *   2. rootport_label (DP is RP of another onboard device)
 *   3. switch_dp_inherited_label (DP inherits from UP)
 *   4. slot_label
 *
 * Returns: label length on success, -1 if no match.
 */
static int syno_dt_get_label(struct pci_dev *pdev, char *buf,
			     enum syno_dt_attr_enum attr)
{
	int len;

	len = syno_dt_get_onboard_dev_label(pdev, buf, attr);
	if (len > 0) {
		return len;
	}
	len = syno_dt_get_rootport_label(pdev, buf, attr);
	if (len > 0) {
		return len;
	}
	len = syno_dt_get_switch_dp_inherited_label(pdev, buf, attr);
	if (len > 0) {
		return len;
	}
	return syno_dt_get_slot_label(pdev, buf, attr);
}

static bool device_has_syno_dt_label(struct pci_dev *pdev)
{
	return syno_dt_get_label(pdev, NULL, SYNO_DT_ATTR_LABEL_SHOW) > 0;
}

static umode_t syno_dt_label_exist(struct kobject *kobj,
				   struct attribute *attr, int n)
{
	struct device *dev;
	struct pci_dev *pdev;

	dev = kobj_to_dev(kobj);
	pdev = to_pci_dev(dev);

	return device_has_syno_dt_label(pdev) ? S_IRUGO : 0;
}

static ssize_t syno_dt_label_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return syno_dt_get_label(pdev, buf, SYNO_DT_ATTR_LABEL_SHOW);
}

static ssize_t syno_dt_index_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return syno_dt_get_label(pdev, buf, SYNO_DT_ATTR_INDEX_SHOW);
}

static struct device_attribute syno_dt_attr_label = {
	.attr = {.name = "label", .mode = 0444},
	.show = syno_dt_label_show,
};

static struct device_attribute syno_dt_attr_index = {
	.attr = {.name = "index", .mode = 0444},
	.show = syno_dt_index_show,
};

static struct attribute *syno_dt_attributes[] = {
	&syno_dt_attr_label.attr,
	&syno_dt_attr_index.attr,
	NULL,
};

static const struct attribute_group syno_dt_attr_group = {
	.attrs = syno_dt_attributes,
	.is_visible = syno_dt_label_exist,
};

static int pci_create_syno_dt_label_files(struct pci_dev *pdev)
{
	return sysfs_create_group(&pdev->dev.kobj, &syno_dt_attr_group);
}

static void pci_remove_syno_dt_label_files(struct pci_dev *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &syno_dt_attr_group);
}

/**
 * pci_get_syno_label - get Synology DTS label for a PCI device
 * @pdev: the PCI device
 * @buf: buffer to store label
 * @size: size of buffer
 *
 * Returns the label length on success, or -1 if no label found.
 * The returned string does NOT have a trailing newline.
 */
int pci_get_syno_label(struct pci_dev *pdev, char *buf, size_t size)
{
	char tmp[SYNO_PCI_LABEL_MAX_LEN] = {'\0'};
	int len = 0;

	len = syno_dt_get_label(pdev, tmp, SYNO_DT_ATTR_LABEL_SHOW);
	if (len <= 0) {
		return -1;
	}

	/* Remove trailing newline */
	if (len > 0 && tmp[len - 1] == '\n') {
		tmp[--len] = '\0';
	}

	if (buf && size > 0) {
		strncpy(buf, tmp, size - 1);
		buf[size - 1] = '\0';
	}

	return len;
}

void pci_create_firmware_label_files(struct pci_dev *pdev)
{
	/* First try Synology device tree labels */
	if (device_has_syno_dt_label(pdev)) {
		pci_create_syno_dt_label_files(pdev);
	}
	/* Then try ACPI DSM */
	else if (device_has_dsm(&pdev->dev)) {
		pci_create_acpi_index_label_files(pdev);
	}
	/* Finally fallback to SMBIOS */
	else {
		pci_create_smbiosname_file(pdev);
	}
}

void pci_remove_firmware_label_files(struct pci_dev *pdev)
{
	/* Remove in the same priority order as creation */
	if (device_has_syno_dt_label(pdev)) {
		pci_remove_syno_dt_label_files(pdev);
	}
	else if (device_has_dsm(&pdev->dev)) {
		pci_remove_acpi_index_label_files(pdev);
	}
	else {
		pci_remove_smbiosname_file(pdev);
	}
}
#else /* CONFIG_SYNO_PCI_DTS_LABEL */
void pci_create_firmware_label_files(struct pci_dev *pdev)
{
	if (device_has_dsm(&pdev->dev))
		pci_create_acpi_index_label_files(pdev);
	else
		pci_create_smbiosname_file(pdev);
}

void pci_remove_firmware_label_files(struct pci_dev *pdev)
{
	if (device_has_dsm(&pdev->dev))
		pci_remove_acpi_index_label_files(pdev);
	else
		pci_remove_smbiosname_file(pdev);
}
#endif /* CONFIG_SYNO_PCI_DTS_LABEL */

