#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wellswang");
MODULE_DESCRIPTION("Functional test for I2C buses' recovery");

static inline void i2c_logf(const struct i2c_adapter *adap,
                            const char *fmt, ...)
{
    struct va_format vaf;
    va_list args;
    const char *device_name = dev_name(&adap->dev);
    const char *ctrlr_name = adap->dev.parent ? dev_name(adap->dev.parent) : "no-controller";

    va_start(args, fmt);
    vaf.fmt = fmt;
    vaf.va = &args;

    printk(KERN_INFO "[%s / %s] %pV", device_name, ctrlr_name, &vaf);
    va_end(args);
}

#define i2c_log(adap, fmt, ...) \
    i2c_logf((adap), fmt, ##__VA_ARGS__)

#define PIN_HIGH 1
#define PIN_LOW  0
static int scl_toggle(struct device *dev, void *data)
{
    struct i2c_adapter *adap = to_i2c_adapter(dev);
	struct i2c_bus_recovery_info *bri = NULL;
	int scl_pin_index = -1;
    int ret = 0;

	if (dev->type != &i2c_adapter_type) {
		goto END;
	}
	if (!adap->syno_init_recovery_info) {
		i2c_log(adap, "No syno_init_recovery_info\n");
		goto END;
	}
	adap->syno_init_recovery_info(adap);

	bri = adap->bus_recovery_info;
	if (!bri || !bri->recover_bus) {
		i2c_log(adap, "No recover function\n");
		goto END;
	}

	if (!bri->scl_gpiod ||
		!bri->set_scl ||
		!bri->get_scl) {
		i2c_log(adap, "No scl definded\n");
		goto END;
	}
	scl_pin_index = syno_desc_to_gpio(bri->scl_gpiod);

	// Set pin MUX to GPIO mode (optional by platform GPIO design)
	if (bri->syno_mux_ops.set_scl_mux) {
		bri->syno_mux_ops.set_scl_mux(adap, bri->syno_mux_ops.scl_mode_gpio);
	}

	// Toggle SCL pin as GPIO and check value
	bri->set_scl(adap, PIN_HIGH);
	if (bri->get_scl(adap) != PIN_HIGH) {
		i2c_log(adap, "scl (%d) set high failed\n", scl_pin_index);
		goto END;
	}
	bri->set_scl(adap, PIN_LOW);
	if (bri->get_scl(adap) != PIN_LOW) {
		i2c_log(adap, "scl (%d) set low failed\n", scl_pin_index);
		goto END;
	}

	i2c_log(adap, "scl (%d) toggled success\n", scl_pin_index);

	// Set pin MUX to back to Native (i.e., i2c) mode (optional by platform GPIO design)
	if (bri->syno_mux_ops.set_scl_mux) {
		bri->syno_mux_ops.set_scl_mux(adap, bri->syno_mux_ops.scl_mode_i2c);
	}
END:
    return ret;
}

static int __init syno_i2c_recovery_check_init(void)
{
    pr_info("syno_i2c_recovery_check module loaded\n");
    i2c_for_each_dev(NULL, scl_toggle);
    return 0;
}

static void __exit syno_i2c_recovery_check_exit(void)
{
    pr_info("syno_i2c_recovery_check module unloaded\n");
}

module_init(syno_i2c_recovery_check_init);
module_exit(syno_i2c_recovery_check_exit);
