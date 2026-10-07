/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FIEMAP_H
#define _LINUX_FIEMAP_H 1

#include <uapi/linux/fiemap.h>
#include <linux/fs.h>

struct fiemap_extent_info {
	unsigned int fi_flags;		/* Flags as passed from user */
	unsigned int fi_extents_mapped;	/* Number of mapped extents */
	unsigned int fi_extents_max;	/* Size of fiemap_extent array */
	struct fiemap_extent __user *fi_extents_start; /* Start of
							fiemap_extent array */
#ifdef CONFIG_SYNO_FS_GENERIC_FIEMAP_FOR_KERNEL_SPACE
	struct fiemap_extent *kernel_fi_extents_start; /* Start of
							fiemap_extent array for kernel */
#endif /* CONFIG_SYNO_FS_GENERIC_FIEMAP_FOR_KERNEL_SPACE */
};

int fiemap_prep(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 *len, u32 supported_flags);
int fiemap_fill_next_extent(struct fiemap_extent_info *info, u64 logical,
			    u64 phys, u64 len, u32 flags);

int generic_block_fiemap(struct inode *inode,
		struct fiemap_extent_info *fieinfo, u64 start, u64 len,
		get_block_t *get_block);

#ifdef CONFIG_SYNO_FS_GENERIC_FIEMAP_FOR_KERNEL_SPACE
int vfs_fiemap(struct file *filp, struct fiemap *fiemap);
#endif /* CONFIG_SYNO_FS_GENERIC_FIEMAP_FOR_KERNEL_SPACE */

#endif /* _LINUX_FIEMAP_H 1 */
