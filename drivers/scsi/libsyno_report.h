// Copyright (c) 2000-2020 Synology Inc. All rights reserved.
#ifndef _SCSI_LIBSYNO_REPORT_H
#define _SCSI_LIBSYNO_REPORT_H
#include <scsi/scsi_device.h>

#ifdef CONFIG_SYNO_SCSI_DISK_ERROR_REPORT
int SynoScsiDeviceToDiskIndex(const struct scsi_device *psdev);

void SynoSendScsiErrorEvent(struct work_struct *work);

void SynoScsiErrorWithSenseReport(struct scsi_device *psdev,
		u8 sense_key, u8 asc, u8 ascq, sector_t lba);

void SynoScsiTimeoutReport(struct scsi_device *psdev,
		unsigned char op, int iRetries);

bool SynoIsPhysicalDrive(const struct scsi_device *psdev);
#endif /* CONFIG_SYNO_SCSI_DISK_ERROR_REPORT */

#endif /* _SCSI_LIBSYNO_REPORT_H */

