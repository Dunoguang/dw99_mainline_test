/*
 * drivers/staging/android/smart_reclaim.c
 *
 * Copyright (C) huawei company
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/version.h>

extern int zap_vma_for_reaping(struct vm_area_struct *vma);

/* User knob to show soft reclaim feature */
static char *soft_reclaim = ".apk .dex .jar .odex";
module_param_named(soft_reclaim, soft_reclaim, charp, S_IRUGO);

/* Below function is same as "madvise_dontneed() in mm/advise.c */
static long sshrinker_madvise_dontneed(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	if (vma->vm_flags & (VM_LOCKED | VM_HUGETLB | VM_PFNMAP))
		return -EINVAL;
	zap_vma_for_reaping(vma);
	return 0;
}

const char *get_path_ext(const char *path)
{
	const char *dot = NULL;

	if (!path)
		return NULL;
	dot = strrchr(path, '.');
	if (!dot || dot == path)
		return NULL;
	return dot + 1;
}

static bool is_soft_vma(struct vm_area_struct *vma)
{
	const char *path;
	const char *suffix;
	bool ret = false;

	if (!vma->vm_file)
		goto out;

	if (vma->anon_vma) {
		/*
		 * For some readonly or rw vma, a file mapping that has
		 * had some COW done. Since pages might have been
		 * written, if free, the data will loss
		*/
		goto out;
	}
	path = vma->vm_file->f_path.dentry->d_name.name;
	/* see the path is soft_reclaim */
	suffix = get_path_ext(path);
	if (suffix && strstr(soft_reclaim, suffix))
		/* printk("need to reclaim vma path=%s\n", path);*/
		ret = true;
out:
	return ret;
}

void smart_soft_shrink(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	MA_STATE(mas, &mm->mm_mt, 0, 0);

	mmap_read_lock(mm);
	mas_for_each(&mas, vma, ULONG_MAX) {
		if (is_soft_vma(vma))
			sshrinker_madvise_dontneed(vma,
				vma->vm_start, vma->vm_end);
	}
	mmap_read_unlock(mm);
}

static int __init smart_reclaim_init(void)
{
	return 0;
}

static void __exit smart_reclaim_exit(void)
{
}

module_init(smart_reclaim_init);
module_exit(smart_reclaim_exit);

MODULE_LICENSE("GPL");
