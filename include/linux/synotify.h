/*
 * Copyright (C) 2021 synology
 */
#ifndef _LINUX_SYNOTIFY_H
#define _LINUX_SYNOTIFY_H

#ifdef CONFIG_SYNO_FS_SYNOTIFY
#include <linux/sysctl.h>
#include <uapi/linux/synotify.h>

extern struct ctl_table synotify_table[]; /* for sysctl */
#endif /* CONFIG_SYNO_FS_SYNOTIFY */

#endif /* _LINUX_SYNOTIFY_H */
