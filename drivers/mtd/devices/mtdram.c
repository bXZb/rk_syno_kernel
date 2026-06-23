/*
 * mtdram - a test mtd device
 * Author: Alexander Larsson <alex@cendio.se>
 *
 * Copyright (c) 1999 Alexander Larsson <alex@cendio.se>
 * Copyright (c) 2005 Joern Engel <joern@wh.fh-wedel.de>
 *
 * This code is GPL
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/mtdram.h>
#include <linux/mtd/partitions.h>
#include <linux/string.h>

static unsigned long total_size = CONFIG_MTDRAM_TOTAL_SIZE;
static unsigned long erase_size = CONFIG_MTDRAM_ERASE_SIZE;
static unsigned long writebuf_size = 64;
static bool syno_msys_parts = true;
static char *syno_rd_upgrade_dev = "PARTLABEL=rdnew";
#define MTDRAM_TOTAL_SIZE (total_size * 1024)
#define MTDRAM_ERASE_SIZE (erase_size * 1024)
#define SYNO_MSYS_RD_OFFSET 0x8d0000
#define SYNO_MSYS_RD_SIZE 0x705000
#define SYNO_RDUP_MAGIC 0x52445550 /* RDUP */
#define SYNO_RDUP_VERSION 1
#define SYNO_RDUP_HDR_SIZE 4096
#define SYNO_RDUP_FLAG_COMPLETE BIT(0)
#define SYNO_MSYS_FIS_DIRECTORY_OFFSET 0xfff000

module_param(total_size, ulong, 0);
MODULE_PARM_DESC(total_size, "Total device size in KiB");
module_param(erase_size, ulong, 0);
MODULE_PARM_DESC(erase_size, "Device erase block size in KiB");
module_param(writebuf_size, ulong, 0);
MODULE_PARM_DESC(writebuf_size, "Device write buf size in Bytes (Default: 64)");
module_param(syno_msys_parts, bool, 0);
MODULE_PARM_DESC(syno_msys_parts, "Register 8 Synology-style RAM MTD partitions");
module_param(syno_rd_upgrade_dev, charp, 0644);
MODULE_PARM_DESC(syno_rd_upgrade_dev, "Block device for mirrored Synology rd.gz upgrade image");

// We could store these in the mtd structure, but we only support 1 device..
static struct mtd_info *mtd_info;
static u32 syno_rdup_image_size;

struct syno_rdup_header {
	__le32 magic;
	__le32 version;
	__le32 header_size;
	__le32 image_size;
	__le32 image_crc32;
	__le32 flags;
	__le32 reserved[10];
};

extern char gszSerialNum[32];
extern unsigned char grgbLanMac[][16];

static void fill_syno_vendor(void *vendor_base)
{
	unsigned char *ptr = (unsigned char *)vendor_base;
	char sn_buf[64] = {0};
	unsigned int sum = 0;
	unsigned char uchar_sum = 0;
	unsigned char mac_bytes[6] = {0};
	char local_mac[16] = {0};
	int i;

	// fallback defaults if not provided in boot cmdline
	if (grgbLanMac[0][0] == '\0') {
		strcpy(local_mac, "021132423001");
	} else {
		strncpy(local_mac, (char *)grgbLanMac[0], sizeof(local_mac) - 1);
	}

	// 1. Header (16 bytes)
	memset(ptr, 0, 16);
	memcpy(ptr, "SYNO!!!!", 8);

	// 2. SN (32 bytes, offset 16)
	if (strlen(gszSerialNum) > 0) {
		for (i = 0; gszSerialNum[i] != '\0'; i++) {
			sum += (unsigned int)gszSerialNum[i];
		}
		snprintf(sn_buf, sizeof(sn_buf), "SN=%s,CHK=%u", gszSerialNum, sum);
		memset(ptr + 16, 0, 32);
		strncpy(ptr + 16, sn_buf, 31);
	}

	// 3. Custom SN (32 bytes, offset 48)
	memset(ptr + 48, 0, 32);
	if (strlen(gszSerialNum) > 0) {
		strncpy(ptr + 48, gszSerialNum, 30);
		uchar_sum = 0;
		for (i = 0; i < 31; i++) {
			uchar_sum += ptr[48 + i];
		}
		ptr[48 + 31] = uchar_sum;
	}

	// 4. Test Flag (128 bytes, offset 80)
	memset(ptr + 80, 0, 128);

	// 5. MAC (offset 208)
	memset(ptr + 208, 0xff, 8 * 7); // Default to 0xff
	if (strlen(local_mac) == 12) {
		for (i = 0; i < 6; i++) {
			mac_bytes[i] = (hex_to_bin(local_mac[i * 2]) << 4) | hex_to_bin(local_mac[i * 2 + 1]);
		}
		uchar_sum = 0;
		for (i = 0; i < 6; i++) {
			ptr[208 + i] = mac_bytes[i];
			uchar_sum += mac_bytes[i];
		}
		ptr[208 + 6] = uchar_sum;
	}
}

struct syno_msys_fis_desc {
	unsigned char name[16];
	u32 flash_base;
	u32 mem_base;
	u32 size;
	u32 entry_point;
	u32 data_length;
	unsigned char _pad[212];
	u32 desc_cksum;
	u32 file_cksum;
};

static const struct mtd_partition syno_msys_partitions[] = {
	{ .name = "RedBoot", .offset = 0x000000, .size = 0x190000 },
	{ .name = "zImage", .offset = 0x190000, .size = 0x730000 },
	{ .name = "dtb", .offset = 0x8c0000, .size = 0x010000 },
	{ .name = "rd.gz", .offset = 0x8d0000, .size = 0x705000 },
	{ .name = "vendor", .offset = 0xfd5000, .size = 0x010000 },
	{ .name = "pstore", .offset = 0xfe5000, .size = 0x018000 },
	{ .name = "Misc Info", .offset = 0xffd000, .size = 0x002000 },
	{ .name = "FIS directory", .offset = 0xfff000, .size = 0x001000 },
};

static const struct mtd_partition syno_msys_fis_partitions[] = {
	{ .name = "RedBoot", .offset = 0x000000, .size = 0x190000 },
	{ .name = "zImage", .offset = 0x190000, .size = 0x730000 },
	{ .name = "dtb", .offset = 0x8c0000, .size = 0x010000 },
	{ .name = "rd.gz", .offset = 0x8d0000, .size = 0x705000 },
	{ .name = "vendor", .offset = 0xfd5000, .size = 0x010000 },
	{ .name = "pstore", .offset = 0xfe5000, .size = 0x018000 },
	{ .name = "Misc Info", .offset = 0xffd000, .size = 0x002000 },
	{ .name = "FIS directory", .offset = 0xfff000, .size = 0x001000 },
};

static void syno_msys_write_fis_table(void *base)
{
	struct syno_msys_fis_desc *fis;
	int i;

	fis = (struct syno_msys_fis_desc *)base;
	for (i = 0; i < ARRAY_SIZE(syno_msys_fis_partitions); i++) {
		memset(&fis[i], 0, sizeof(fis[i]));
		strscpy(fis[i].name, syno_msys_fis_partitions[i].name,
			sizeof(fis[i].name));
		fis[i].flash_base = syno_msys_fis_partitions[i].offset;
		fis[i].size = syno_msys_fis_partitions[i].size;
		fis[i].data_length = syno_msys_fis_partitions[i].size;
	}
}

static void syno_msys_seed_fis_tables(void *mapped_address)
{
	syno_msys_write_fis_table((char *)mapped_address +
				  SYNO_MSYS_FIS_DIRECTORY_OFFSET);
}

static int check_offs_len(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	int ret = 0;

	/* Start address must align on block boundary */
	if (mtd_mod_by_eb(ofs, mtd)) {
		pr_debug("%s: unaligned address\n", __func__);
		ret = -EINVAL;
	}

	/* Length must align on block boundary */
	if (mtd_mod_by_eb(len, mtd)) {
		pr_debug("%s: length not block aligned\n", __func__);
		ret = -EINVAL;
	}

	return ret;
}

static int ram_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	if (check_offs_len(mtd, instr->addr, instr->len))
		return -EINVAL;
	memset((char *)mtd->priv + instr->addr, 0xff, instr->len);

	return 0;
}

static int ram_point(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, void **virt, resource_size_t *phys)
{
	*virt = mtd->priv + from;
	*retlen = len;

	if (phys) {
		/* limit retlen to the number of contiguous physical pages */
		unsigned long page_ofs = offset_in_page(*virt);
		void *addr = *virt - page_ofs;
		unsigned long pfn1, pfn0 = vmalloc_to_pfn(addr);

		*phys = __pfn_to_phys(pfn0) + page_ofs;
		len += page_ofs;
		while (len > PAGE_SIZE) {
			len -= PAGE_SIZE;
			addr += PAGE_SIZE;
			pfn0++;
			pfn1 = vmalloc_to_pfn(addr);
			if (pfn1 != pfn0) {
				*retlen = addr - *virt;
				break;
			}
		}
	}

	return 0;
}

static int ram_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	return 0;
}

static int ram_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	memcpy(buf, mtd->priv + from, len);
	*retlen = len;
	return 0;
}

static bool syno_msys_rd_write_range(loff_t to, size_t len, loff_t *pos,
				     size_t *mirror_len, size_t *buf_off)
{
	loff_t start;
	size_t remain;

	if (!syno_msys_parts || !syno_rd_upgrade_dev || !*syno_rd_upgrade_dev)
		return false;
	if (!len || to >= SYNO_MSYS_RD_OFFSET + SYNO_MSYS_RD_SIZE)
		return false;

	start = max_t(loff_t, to, SYNO_MSYS_RD_OFFSET);
	*pos = start - SYNO_MSYS_RD_OFFSET;
	*buf_off = start - to;
	if (*buf_off >= len)
		return false;

	remain = SYNO_MSYS_RD_SIZE - *pos;
	*mirror_len = min_t(size_t, len - *buf_off, remain);

	return true;
}

static int syno_rdup_open_bdev(struct block_device **bdev)
{
	const fmode_t mode = FMODE_READ | FMODE_WRITE;
	dev_t devt;

	*bdev = blkdev_get_by_path(syno_rd_upgrade_dev, mode, mtd_info);
	if (!IS_ERR(*bdev))
		return 0;

	devt = name_to_dev_t(syno_rd_upgrade_dev);
	if (!devt)
		return PTR_ERR(*bdev);

	*bdev = blkdev_get_by_dev(devt, mode, mtd_info);
	if (IS_ERR(*bdev))
		return PTR_ERR(*bdev);

	return 0;
}

static int syno_rdup_bdev_write(struct block_device *bdev, loff_t to,
				const u_char *buf, size_t len)
{
	struct address_space *mapping = bdev->bd_inode->i_mapping;
	struct page *page;
	pgoff_t index = to >> PAGE_SHIFT;
	size_t offset = to & (PAGE_SIZE - 1);
	size_t cpylen;

	while (len) {
		cpylen = min_t(size_t, len, PAGE_SIZE - offset);

		page = read_mapping_page(mapping, index, NULL);
		if (IS_ERR(page))
			return PTR_ERR(page);

		lock_page(page);
		memcpy(page_address(page) + offset, buf, cpylen);
		set_page_dirty(page);
		unlock_page(page);
		put_page(page);

		balance_dirty_pages_ratelimited(mapping);
		buf += cpylen;
		len -= cpylen;
		offset = 0;
		index++;
	}

	return 0;
}

static void syno_rdup_write_header(struct block_device *bdev)
{
	struct syno_rdup_header header = {
		.magic = cpu_to_le32(SYNO_RDUP_MAGIC),
		.version = cpu_to_le32(SYNO_RDUP_VERSION),
		.header_size = cpu_to_le32(SYNO_RDUP_HDR_SIZE),
		.image_size = cpu_to_le32(0),
		.image_crc32 = cpu_to_le32(0),
		.flags = cpu_to_le32(0),
	};
	int ret;

	ret = syno_rdup_bdev_write(bdev, 0, (u_char *)&header, sizeof(header));
	if (ret)
		pr_warn_ratelimited("mtdram: failed to write rd upgrade header: %d\n",
				    ret);
}

static void syno_rdup_update_size(struct block_device *bdev, u32 image_size)
{
	__le32 size = cpu_to_le32(image_size);
	int ret;

	ret = syno_rdup_bdev_write(bdev, offsetof(struct syno_rdup_header,
						  image_size),
				   (u_char *)&size, sizeof(size));
	if (ret)
		pr_warn_ratelimited("mtdram: failed to update rd upgrade size: %d\n",
				    ret);
}

static void syno_rdup_update_flags(struct block_device *bdev, u32 flags)
{
	__le32 value = cpu_to_le32(flags);
	int ret;

	ret = syno_rdup_bdev_write(bdev, offsetof(struct syno_rdup_header,
						  flags),
				   (u_char *)&value, sizeof(value));
	if (ret)
		pr_warn_ratelimited("mtdram: failed to update rd upgrade flags: %d\n",
				    ret);
}

static void syno_msys_mirror_rd_write(loff_t to, size_t len, const u_char *buf)
{
	struct block_device *bdev;
	loff_t pos, image_end;
	size_t mirror_len;
	size_t buf_off;
	int ret;

	if (!syno_msys_rd_write_range(to, len, &pos, &mirror_len, &buf_off))
		return;

	ret = syno_rdup_open_bdev(&bdev);
	if (ret) {
		pr_warn_ratelimited("mtdram: failed to open rd upgrade block device %s: %d\n",
				    syno_rd_upgrade_dev, ret);
		return;
	}

	if (!pos) {
		syno_rdup_image_size = 0;
		syno_rdup_write_header(bdev);
	}

	ret = syno_rdup_bdev_write(bdev, SYNO_RDUP_HDR_SIZE + pos,
				   buf + buf_off, mirror_len);
	if (ret) {
		pr_warn_ratelimited("mtdram: failed to mirror rd.gz write: %d\n",
				    ret);
		goto out_put;
	}

	image_end = pos + mirror_len;
	if (image_end <= U32_MAX) {
		syno_rdup_image_size = max_t(u32, syno_rdup_image_size,
					     image_end);
		syno_rdup_update_size(bdev, image_end);
	}

	sync_blockdev(bdev);

out_put:
	blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
}

static void ram_sync(struct mtd_info *mtd)
{
	struct block_device *bdev;
	int ret;

	if (!syno_rdup_image_size)
		return;

	ret = syno_rdup_open_bdev(&bdev);
	if (ret)
		return;

	syno_rdup_update_size(bdev, syno_rdup_image_size);
	syno_rdup_update_flags(bdev, SYNO_RDUP_FLAG_COMPLETE);
	sync_blockdev(bdev);
	blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
}

static int ram_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	memcpy((char *)mtd->priv + to, buf, len);
	*retlen = len;
	syno_msys_mirror_rd_write(to, len, buf);
	return 0;
}

static int ram_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	return 0;
}

static int ram_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	return 0;
}

static int ram_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	return 0;
}

static void __exit cleanup_mtdram(void)
{
	if (mtd_info) {
		mtd_device_unregister(mtd_info);
		vfree(mtd_info->priv);
		kfree(mtd_info);
	}
}

int mtdram_init_device(struct mtd_info *mtd, void *mapped_address,
		unsigned long size, const char *name)
{
	memset(mtd, 0, sizeof(*mtd));

	/* Setup the MTD structure */
	mtd->name = name;
	mtd->type = MTD_RAM;
	mtd->flags = MTD_CAP_RAM;
	mtd->size = size;
	mtd->writesize = 1;
	mtd->writebufsize = writebuf_size;
	mtd->erasesize = MTDRAM_ERASE_SIZE;
	mtd->priv = mapped_address;

	mtd->owner = THIS_MODULE;
	mtd->_erase = ram_erase;
	mtd->_point = ram_point;
	mtd->_unpoint = ram_unpoint;
	mtd->_read = ram_read;
	mtd->_write = ram_write;
	mtd->_sync = ram_sync;
	mtd->_lock = ram_lock;
	mtd->_unlock = ram_unlock;
	mtd->_is_locked = ram_is_locked;

	if (syno_msys_parts) {
		mtd->type = MTD_NORFLASH;
		mtd->flags = MTD_CAP_NORFLASH;
		if (mtd_device_register(mtd, syno_msys_partitions,
		    ARRAY_SIZE(syno_msys_partitions)))
			return -EIO;
	} else if (mtd_device_register(mtd, NULL, 0)) {
		return -EIO;
	}

	return 0;
}

static int __init init_mtdram(void)
{
	void *addr;
	int err;

	if (!total_size)
		return -EINVAL;

	/* Allocate some memory */
	mtd_info = kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (!mtd_info)
		return -ENOMEM;

	addr = vmalloc(MTDRAM_TOTAL_SIZE);
	if (!addr) {
		kfree(mtd_info);
		mtd_info = NULL;
		return -ENOMEM;
	}
	memset(addr, 0xff, MTDRAM_TOTAL_SIZE);
	if (syno_msys_parts) {
		syno_msys_seed_fis_tables(addr);
		fill_syno_vendor((char *)addr + 0xfd5000);
	}
	err = mtdram_init_device(mtd_info, addr, MTDRAM_TOTAL_SIZE, "mtdram test device");
	if (err) {
		vfree(addr);
		kfree(mtd_info);
		mtd_info = NULL;
		return err;
	}
	return err;
}

module_init(init_mtdram);
module_exit(cleanup_mtdram);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Larsson <alexl@redhat.com>");
MODULE_DESCRIPTION("Simulated MTD driver for testing");
