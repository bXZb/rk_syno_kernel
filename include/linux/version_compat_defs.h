/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VERSION_COMPAT_DEFS_H
#define _LINUX_VERSION_COMPAT_DEFS_H

#include <linux/mm.h>

#ifndef vm_flags_set
static inline void vm_flags_set(struct vm_area_struct *vma, vm_flags_t flags)
{
	vma->vm_flags |= flags;
}
#endif

#ifndef vm_flags_clear
static inline void vm_flags_clear(struct vm_area_struct *vma, vm_flags_t flags)
{
	vma->vm_flags &= ~flags;
}
#endif

#endif /* _LINUX_VERSION_COMPAT_DEFS_H */
