/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 *//*
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <linux/maple_tree.h>
#include <linux/mman.h>
#include <linux/mm.h>

#include "../mali_kbase.h"

/*
 * Find the highest suitable start in an empty interval [start, end).
 * The GPU address must retain its alignment colour even when it is moved
 * below a 4 GiB boundary.
 */
static bool kbase_align_gap(unsigned long start, unsigned long end,
			    const struct vm_unmapped_area_info *info,
			    bool shader, bool same_4gb_page,
			    unsigned long *result)
{
	unsigned long stride = info->align_mask + 1;
	unsigned long addr, offset;
	int i;

	if (start > end || info->length > end - start)
		return false;

	addr = end - info->length;
	offset = (addr - info->align_offset) & info->align_mask;
	if (offset > addr - start)
		return false;
	addr -= offset;

	if (shader) {
		/*
		 * The two forbidden colours in a 4 GiB window are a
		 * boundary at the start or at the end of the mapping.
		 */
		for (i = 0; i < 2 &&
		     (!(addr & BASE_MEM_MASK_4GB) ||
		      !((addr + info->length) & BASE_MEM_MASK_4GB)); i++) {
			if (stride > addr - start)
				return false;
			addr -= stride;
		}

		if (!(addr & BASE_MEM_MASK_4GB) ||
		    !((addr + info->length) & BASE_MEM_MASK_4GB))
			return false;
	} else if (same_4gb_page) {
		unsigned long boundary = (addr + info->length - 1) &
					 ~((unsigned long)U32_MAX);

		if (addr < boundary) {
			if (boundary < start || info->length > boundary - start)
				return false;

			addr = boundary - info->length;
			offset = (addr - info->align_offset) & info->align_mask;
			if (offset > addr - start)
				return false;
			addr -= offset;
		}

		if ((addr & ~((unsigned long)U32_MAX)) !=
		    ((addr + info->length - 1) & ~((unsigned long)U32_MAX)))
			return false;
	}

	*result = addr;
	return true;
}

/*
 * The exported architecture allocator accepts a usable hint verbatim.
 * Checking a raw Maple Tree gap this way honours the native stack guard
 * and mmap minimum without accessing unexported MM state.
 */
static unsigned long kbase_test_hint(unsigned long addr, unsigned long length)
{
	return mm_get_unmapped_area(NULL, addr, length, 0, 0);
}

static unsigned long kbase_unmapped_area_topdown(
		const struct vm_unmapped_area_info *info, bool shader,
		bool same_4gb_page)
{
	struct mm_struct *mm = current->mm;
	unsigned long low = info->low_limit;
	unsigned long high = info->high_limit;

	mmap_assert_locked(mm);

	while (high > low && info->length <= high - low) {
		MA_STATE(mas, &mm->mm_mt, 0, 0);
		struct vm_area_struct *prev;
		unsigned long start, end, addr, hint;

		if (mas_empty_area_rev(&mas, low, high - 1, info->length))
			return -ENOMEM;

		end = mas.last + 1;
		prev = mas_prev(&mas, 0);
		start = prev ? max(low, prev->vm_end) : low;

		if (kbase_align_gap(start, end, info, shader, same_4gb_page, &addr)) {
			hint = kbase_test_hint(addr, info->length);
			if (hint == addr)
				return addr;

			/*
			 * The top of this raw gap may overlap a stack guard.
			 * Locate the highest usable page in the same gap, then
			 * reapply the GPU-specific colour and boundary rules.
			 */
			if (hint < start || hint >= addr ||
			    kbase_test_hint(hint, info->length) != hint)
				hint = start;

			if (hint < addr && kbase_test_hint(hint, info->length) == hint) {
				unsigned long limit = addr;

				while (limit - hint > PAGE_SIZE) {
					unsigned long mid = hint + ((limit - hint) / 2 & PAGE_MASK);

					if (kbase_test_hint(mid, info->length) == mid)
						hint = mid;
					else
						limit = mid;
				}

				if (kbase_align_gap(start, hint + info->length, info,
						    shader, same_4gb_page, &addr) &&
				    kbase_test_hint(addr, info->length) == addr)
					return addr;
			}
		}

		if (!prev || prev->vm_start <= low)
			break;

		high = prev->vm_start;
	}

	return -ENOMEM;
}

/* This function is based on Linux kernel's arch_get_unmapped_area, but
 * simplified slightly. Modifications come from the fact that some values
 * about the memory area are known in advance.
 */
unsigned long kbase_get_unmapped_area(struct file *filp,
		const unsigned long addr, const unsigned long len,
		const unsigned long pgoff, const unsigned long flags)
{
	struct kbase_context *kctx = filp->private_data;
	struct mm_struct *mm = current->mm;
	struct vm_unmapped_area_info info = {};
	unsigned long align_offset = 0;
	unsigned long align_mask = 0;
	unsigned long high_limit = mm->mmap_base;
	unsigned long low_limit = PAGE_SIZE;
	int cpu_va_bits = BITS_PER_LONG;
	int gpu_pc_bits =
	      kctx->kbdev->gpu_props.props.core_props.log2_program_counter_size;
	bool is_shader_code = false;
	bool is_same_4gb_page = false;
	unsigned long ret;

	/* err on fixed address */
	if ((flags & MAP_FIXED) || addr || !len)
		return -EINVAL;

#ifdef CONFIG_64BIT
	/* too big? */
	if (len > TASK_SIZE - SZ_2M)
		return -ENOMEM;

	if (!kbase_ctx_flag(kctx, KCTX_COMPAT)) {

		high_limit = min_t(unsigned long, mm->mmap_base,
				(kctx->same_va_end << PAGE_SHIFT));

		/* If there's enough (> 33 bits) of GPU VA space, align
		 * to 2MB boundaries.
		 */
		if (kctx->kbdev->gpu_props.mmu.va_bits > 33) {
			if (len >= SZ_2M) {
				align_offset = SZ_2M;
				align_mask = SZ_2M - 1;
			}
		}

		low_limit = SZ_2M;
	} else {
		cpu_va_bits = 32;
	}
#endif /* CONFIG_64BIT */
	if ((PFN_DOWN(BASE_MEM_COOKIE_BASE) <= pgoff) &&
		(PFN_DOWN(BASE_MEM_FIRST_FREE_ADDRESS) > pgoff)) {
			int cookie = pgoff - PFN_DOWN(BASE_MEM_COOKIE_BASE);
			struct kbase_va_region *reg =
					kctx->pending_regions[cookie];

			if (!reg)
				return -EINVAL;

			if (!(reg->flags & KBASE_REG_GPU_NX)) {
				if (cpu_va_bits > gpu_pc_bits) {
					align_offset = 1ULL << gpu_pc_bits;
					align_mask = align_offset - 1;
					is_shader_code = true;
				}
			} else if (reg->flags & KBASE_REG_TILER_ALIGN_TOP) {
				unsigned long extent_bytes =
				     (unsigned long)(reg->extent << PAGE_SHIFT);
				/* kbase_check_alloc_sizes() already satisfies
				 * these checks, but they're here to avoid
				 * maintenance hazards due to the assumptions
				 * involved */
				WARN_ON(reg->extent > (ULONG_MAX >> PAGE_SHIFT));
				WARN_ON(reg->initial_commit > (ULONG_MAX >> PAGE_SHIFT));
				WARN_ON(!is_power_of_2(extent_bytes));
				align_mask = extent_bytes - 1;
				align_offset =
				      extent_bytes - (reg->initial_commit << PAGE_SHIFT);
			} else if (reg->flags & KBASE_REG_GPU_VA_SAME_4GB_PAGE) {
				is_same_4gb_page = true;
			}
#ifndef CONFIG_64BIT
	} else {
		return mm_get_unmapped_area(filp, addr, len, pgoff, flags);
#endif
	}

	info.length = len;
	info.low_limit = low_limit;
	info.high_limit = high_limit;
	info.align_offset = align_offset;
	info.align_mask = align_mask;

	ret = kbase_unmapped_area_topdown(&info, is_shader_code,
			is_same_4gb_page);

	if (IS_ERR_VALUE(ret) && high_limit == mm->mmap_base &&
			high_limit < (kctx->same_va_end << PAGE_SHIFT)) {
		/* Retry above mmap_base */
		info.low_limit = mm->mmap_base;
		info.high_limit = min_t(u64, TASK_SIZE,
					(kctx->same_va_end << PAGE_SHIFT));

		ret = kbase_unmapped_area_topdown(&info, is_shader_code,
				is_same_4gb_page);
	}

	return ret;
}
