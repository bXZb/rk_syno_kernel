/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2000-2021 Synology Inc.
 */
#ifndef _LIBMD_REPORT_H
#define _LIBMD_REPORT_H

#ifdef CONFIG_SYNO_MD_SECTOR_STATUS_REPORT
extern int (*funcSYNOSendRaidEvent)(unsigned int type, unsigned int raidno,
				    unsigned int diskno, unsigned long long sector);

void syno_report_bad_sector(sector_t sector, unsigned long rw,
			    int md_minor, struct block_device *bdev, const char *func_name);

void syno_report_uncorrected_bad_sector(sector_t sector, int md_minor,
					struct block_device *bdev, const char *func_name);

void syno_report_correct_bad_sector(sector_t sector, int md_minor,
				    struct block_device *bdev, const char *func_name);

void syno_report_faulty_device(int md_minor, struct block_device *bdev);
#endif /* CONFIG_SYNO_MD_SECTOR_STATUS_REPORT */
#ifdef CONFIG_SYNO_MD_AUTO_REMAP_REPORT
extern int (*funcSYNOSendAutoRemapRaidEvent)(unsigned int, unsigned long long, unsigned int);
#endif /* CONFIG_SYNO_MD_AUTO_REMAP_REPORT */
#endif /* _LIBMD_REPORT_H */

