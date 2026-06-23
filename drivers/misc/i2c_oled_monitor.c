// SPDX-License-Identifier: GPL-2.0
/*
 * I2C OLED monitor for small SSD1306 compatible 128x64 panels.
 */

#include <linux/cpumask.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/font.h>
#include <linux/i2c.h>
#include <linux/inetdevice.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sysinfo.h>
#include <linux/thermal.h>
#include <linux/time64.h>
#include <linux/workqueue.h>

#define I2C_OLED_WIDTH			128
#define I2C_OLED_HEIGHT			64
#define I2C_OLED_PAGES			(I2C_OLED_HEIGHT / 8)
#define I2C_OLED_BUFSZ			(I2C_OLED_WIDTH * I2C_OLED_PAGES)
#define I2C_OLED_MAX_TEXT		96
#define I2C_OLED_DEFAULT_INTERVAL_MS	1000

struct i2c_oled_monitor {
	struct i2c_client *client;
	struct delayed_work work;
	struct mutex lock;
	const struct font_desc *font;
	u8 fb[I2C_OLED_BUFSZ];
	char message[I2C_OLED_MAX_TEXT];
	char ifname[IFNAMSIZ];
	u32 interval_ms;
	u64 last_total;
	u64 last_idle;
	u8 cpu_usage;
	bool enabled;
};

static int i2c_oled_write_cmd(struct i2c_oled_monitor *mon, u8 cmd)
{
	u8 buf[2] = { 0x00, cmd };
	int ret;

	ret = i2c_master_send(mon->client, buf, sizeof(buf));

	return ret == sizeof(buf) ? 0 : -EIO;
}

static int i2c_oled_write_data(struct i2c_oled_monitor *mon, const u8 *data,
			      size_t len)
{
	u8 buf[17];
	size_t todo;
	int ret;

	while (len) {
		todo = min_t(size_t, len, sizeof(buf) - 1);
		buf[0] = 0x40;
		memcpy(&buf[1], data, todo);

		ret = i2c_master_send(mon->client, buf, todo + 1);
		if (ret != todo + 1)
			return -EIO;

		data += todo;
		len -= todo;
	}

	return 0;
}

static int i2c_oled_init_panel(struct i2c_oled_monitor *mon)
{
	static const u8 init[] = {
		0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40,
		0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x12,
		0x81, 0x7f, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6,
		0x2e, 0xaf,
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(init); i++) {
		ret = i2c_oled_write_cmd(mon, init[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int i2c_oled_flush(struct i2c_oled_monitor *mon)
{
	int page, ret;

	for (page = 0; page < I2C_OLED_PAGES; page++) {
		ret = i2c_oled_write_cmd(mon, 0xb0 | page);
		if (ret)
			return ret;
		ret = i2c_oled_write_cmd(mon, 0x00);
		if (ret)
			return ret;
		ret = i2c_oled_write_cmd(mon, 0x10);
		if (ret)
			return ret;
		ret = i2c_oled_write_data(mon, &mon->fb[page * I2C_OLED_WIDTH],
					 I2C_OLED_WIDTH);
		if (ret)
			return ret;
	}

	return 0;
}

static void i2c_oled_clear(struct i2c_oled_monitor *mon)
{
	memset(mon->fb, 0, sizeof(mon->fb));
}

static void i2c_oled_draw_char(struct i2c_oled_monitor *mon, int x, int y, char c)
{
	const u8 *glyph;
	int row, col;

	if (!mon->font || x >= I2C_OLED_WIDTH || y >= I2C_OLED_HEIGHT)
		return;

	if (c < 32 || c > 126)
		c = '?';

	glyph = mon->font->data + c * mon->font->height;

	for (row = 0; row < mon->font->height; row++) {
		int py = y + row;

		if (py >= I2C_OLED_HEIGHT)
			break;

		for (col = 0; col < mon->font->width; col++) {
			int px = x + col;

			if (px >= I2C_OLED_WIDTH)
				break;
			if (glyph[row] & BIT(mon->font->width - 1 - col))
				mon->fb[(py / 8) * I2C_OLED_WIDTH + px] |=
					BIT(py % 8);
		}
	}
}

static void i2c_oled_draw_text(struct i2c_oled_monitor *mon, int row,
			      const char *text)
{
	int x = 0;

	while (*text && x + mon->font->width <= I2C_OLED_WIDTH) {
		i2c_oled_draw_char(mon, x, row * mon->font->height, *text);
		x += mon->font->width;
		text++;
	}
}

static u8 i2c_oled_read_cpu_usage(struct i2c_oled_monitor *mon)
{
	struct kernel_cpustat stat;
	u64 total = 0, idle = 0, diff_total, diff_idle;
	int cpu, i;

	for_each_online_cpu(cpu) {
		kcpustat_cpu_fetch(&stat, cpu);
		for (i = 0; i < NR_STATS; i++)
			total += stat.cpustat[i];
		idle += stat.cpustat[CPUTIME_IDLE] + stat.cpustat[CPUTIME_IOWAIT];
	}

	diff_total = total - mon->last_total;
	diff_idle = idle - mon->last_idle;
	mon->last_total = total;
	mon->last_idle = idle;

	if (!diff_total || diff_idle >= diff_total)
		return mon->cpu_usage;

	mon->cpu_usage = div64_u64((diff_total - diff_idle) * 100, diff_total);

	return mon->cpu_usage;
}

static int i2c_oled_read_temp(void)
{
	struct thermal_zone_device *tz;
	int temp;

	tz = thermal_zone_get_zone_by_name("cpu-thermal");
	if (IS_ERR(tz))
		return INT_MIN;

	if (thermal_zone_get_temp(tz, &temp))
		return INT_MIN;

	return temp / 1000;
}

static unsigned long i2c_oled_read_mem_usage(void)
{
	struct sysinfo i;
	unsigned long total, used;

	si_meminfo(&i);
	total = i.totalram;
	used = i.totalram - i.freeram - i.bufferram;

	if (!total)
		return 0;

	return used * 100 / total;
}

static void i2c_oled_read_ip(struct i2c_oled_monitor *mon, char *buf, size_t len)
{
	struct net_device *ndev;
	struct in_device *in_dev;
	struct in_ifaddr *ifa;
	__be32 addr = 0;
	bool carrier = false;

	rcu_read_lock();
	ndev = dev_get_by_name_rcu(&init_net, mon->ifname);
	if (!ndev) {
		rcu_read_unlock();
		strscpy(buf, "noif", len);
		return;
	}

	carrier = netif_carrier_ok(ndev);
	in_dev = __in_dev_get_rcu(ndev);
	if (in_dev) {
		ifa = rcu_dereference(in_dev->ifa_list);
		if (ifa)
			addr = ifa->ifa_local;
	}
	rcu_read_unlock();

	if (!carrier)
		strscpy(buf, "down", len);
	else if (!addr)
		strscpy(buf, "noip", len);
	else
		snprintf(buf, len, "%pI4", &addr);
}

static void i2c_oled_render(struct i2c_oled_monitor *mon)
{
	struct tm tm;
	time64_t now = ktime_get_real_seconds();
	unsigned long up = ktime_get_seconds();
	char line[32], ip[16];
	int temp = i2c_oled_read_temp();

	time64_to_tm(now, 0, &tm);
	i2c_oled_read_ip(mon, ip, sizeof(ip));
	i2c_oled_clear(mon);

	snprintf(line, sizeof(line), "%04ld-%02d-%02d %02d:%02d",
		 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		 tm.tm_hour, tm.tm_min);
	i2c_oled_draw_text(mon, 0, line);

	snprintf(line, sizeof(line), "CPU %3u%%  MEM %3lu%%",
		 i2c_oled_read_cpu_usage(mon), i2c_oled_read_mem_usage());
	i2c_oled_draw_text(mon, 1, line);

	if (temp == INT_MIN)
		snprintf(line, sizeof(line), "TEMP --C");
	else
		snprintf(line, sizeof(line), "TEMP %dC", temp);
	i2c_oled_draw_text(mon, 2, line);

	snprintf(line, sizeof(line), "UP %lud %02lu:%02lu",
		 up / 86400, up / 3600 % 24, up / 60 % 60);
	i2c_oled_draw_text(mon, 3, line);

	snprintf(line, sizeof(line), "%s %s", mon->ifname, ip);
	i2c_oled_draw_text(mon, 4, line);

	mutex_lock(&mon->lock);
	if (mon->message[0])
		i2c_oled_draw_text(mon, 6, mon->message);
	mutex_unlock(&mon->lock);
}

static void i2c_oled_work(struct work_struct *work)
{
	struct i2c_oled_monitor *mon =
		container_of(to_delayed_work(work), struct i2c_oled_monitor, work);

	i2c_oled_render(mon);
	i2c_oled_flush(mon);

	if (mon->enabled)
		schedule_delayed_work(&mon->work,
				      msecs_to_jiffies(mon->interval_ms));
}

static ssize_t message_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct i2c_oled_monitor *mon = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&mon->lock);
	ret = sysfs_emit(buf, "%s\n", mon->message);
	mutex_unlock(&mon->lock);

	return ret;
}

static ssize_t message_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct i2c_oled_monitor *mon = dev_get_drvdata(dev);
	size_t len = min_t(size_t, count, sizeof(mon->message) - 1);

	mutex_lock(&mon->lock);
	memcpy(mon->message, buf, len);
	mon->message[len] = '\0';
	strim(mon->message);
	mutex_unlock(&mon->lock);

	schedule_delayed_work(&mon->work, 0);

	return count;
}
static DEVICE_ATTR_RW(message);

static ssize_t refresh_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct i2c_oled_monitor *mon = dev_get_drvdata(dev);

	schedule_delayed_work(&mon->work, 0);

	return count;
}
static DEVICE_ATTR_WO(refresh);

static ssize_t enabled_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct i2c_oled_monitor *mon = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", mon->enabled);
}

static ssize_t enabled_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct i2c_oled_monitor *mon = dev_get_drvdata(dev);
	bool enabled;
	int ret;

	ret = kstrtobool(buf, &enabled);
	if (ret)
		return ret;

	mon->enabled = enabled;
	if (enabled)
		schedule_delayed_work(&mon->work, 0);
	else
		cancel_delayed_work_sync(&mon->work);

	return count;
}
static DEVICE_ATTR_RW(enabled);

static struct attribute *i2c_oled_attrs[] = {
	&dev_attr_message.attr,
	&dev_attr_refresh.attr,
	&dev_attr_enabled.attr,
	NULL,
};
ATTRIBUTE_GROUPS(i2c_oled);

static int i2c_oled_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_oled_monitor *mon;
	const char *ifname;
	u32 interval;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	mon = devm_kzalloc(&client->dev, sizeof(*mon), GFP_KERNEL);
	if (!mon)
		return -ENOMEM;

	mon->client = client;
	mon->font = find_font("6x8");
	if (!mon->font)
		return -ENOENT;

	if (of_property_read_u32(client->dev.of_node, "refresh-ms", &interval))
		interval = I2C_OLED_DEFAULT_INTERVAL_MS;
	mon->interval_ms = clamp_t(u32, interval, 250, 60000);

	if (of_property_read_string(client->dev.of_node, "netdev", &ifname))
		ifname = "eth0";
	strscpy(mon->ifname, ifname, sizeof(mon->ifname));

	mutex_init(&mon->lock);
	i2c_set_clientdata(client, mon);
	dev_set_drvdata(&client->dev, mon);

	ret = i2c_oled_init_panel(mon);
	if (ret)
		return ret;

	INIT_DELAYED_WORK(&mon->work, i2c_oled_work);
	mon->enabled = true;
	schedule_delayed_work(&mon->work, 0);

	return 0;
}

static int i2c_oled_remove(struct i2c_client *client)
{
	struct i2c_oled_monitor *mon = i2c_get_clientdata(client);

	mon->enabled = false;
	cancel_delayed_work_sync(&mon->work);
	i2c_oled_clear(mon);
	i2c_oled_flush(mon);
	i2c_oled_write_cmd(mon, 0xae);

	return 0;
}

static const struct of_device_id i2c_oled_of_match[] = {
	{ .compatible = "solomon,ssd1306-oled-monitor" },
	{ }
};
MODULE_DEVICE_TABLE(of, i2c_oled_of_match);

static const struct i2c_device_id i2c_oled_id[] = {
	{ "i2c-oled-monitor", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_oled_id);

static struct i2c_driver i2c_oled_driver = {
	.driver = {
		.name = "i2c-oled-monitor",
		.of_match_table = i2c_oled_of_match,
		.dev_groups = i2c_oled_groups,
	},
	.probe = i2c_oled_probe,
	.remove = i2c_oled_remove,
	.id_table = i2c_oled_id,
};
module_i2c_driver(i2c_oled_driver);

MODULE_DESCRIPTION("I2C OLED system monitor");
MODULE_AUTHOR("yxl");
MODULE_LICENSE("GPL");
