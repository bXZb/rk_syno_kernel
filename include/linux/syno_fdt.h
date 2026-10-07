/* Copyright (c) 2000-2020 Synology Inc. All rights reserved. */
#ifndef __SYNO_FDT_H_
#define __SYNO_FDT_H_

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/device.h>

#ifdef CONFIG_SYNO_HWMON_PMBUS
int syno_pmbus_property_get(unsigned int *pmbus_property, const char *property_name, int index);
#endif /* CONFIG_SYNO_HWMON_PMBUS */
bool syno_of_i2c_driver_match_device(struct device *dev, const struct device_driver *drv);
int syno_of_i2c_mux_bus_match(int parent_bus, int parent_addr, int channel, int* index);
bool is_syno_i2c_bus_bind(void);
struct device_node* syno_of_i2c_bus_match(struct device *dev, int* index);
struct device_node* syno_of_i2c_device_match(struct i2c_client *client, const char *i2c_dev_name, struct device_node *pI2CNode);
struct device_node* syno_of_i2c_adapter_match(struct i2c_adapter *adap);
struct i2c_adapter* syno_i2c_adapter_get_by_node(struct device_node *pI2CBusNode);
int syno_fdt_get_index(int *puiIndex, const char *szNodeName);

int syno_of_get_int_property_recursive(struct device_node *pNode, const char *prop, u32 *out_val);

#ifdef CONFIG_ACPI
struct device_node *syno_of_node_by_acpi_device(const struct platform_device *pdev);
#endif /* CONFIG_ACPI */

#ifdef CONFIG_SYNO_I2C_GENERIC_RECOVERY_BY_DTS
void __iomem *syno_of_iomux_base_by_node(const struct device_node *node);
#endif /* CONFIG_SYNO_I2C_GENERIC_RECOVERY_BY_DTS */

#ifdef CONFIG_SYNO_MICROP_COMMAND_V2
void syno_microp_series_get(void);
#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */
#endif /* __SYNO_FDT_H_ */
