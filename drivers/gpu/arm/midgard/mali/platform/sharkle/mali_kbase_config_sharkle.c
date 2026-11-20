/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/devfreq-governor.h>
#include <linux/clk.h>
#include <linux/hwspinlock.h>
#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#include "mali_kbase_config_platform.h"
#include <mali_kbase_debug.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#ifdef KBASE_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#ifdef CONFIG_OF
#include <linux/of.h>
#endif
#ifdef CONFIG_MALI_HOTPLUG
#include <hotplug/mali_kbase_hotplug.h>
#endif /* CONFIG_MALI_HOTPLUG */
#include <linux/regulator/consumer.h>

#include <linux/smp.h>
#include <trace/events/power.h>
#define VENDOR_FTRACE_MODULE_NAME    "unisoc-gpu"

#define DTS_CLK_OFFSET      2
#define PM_RUNTIME_DELAY_MS 50
#define UP_THRESHOLD        9/10

struct gpu_freq_info {
	struct clk* clk_src;
	int freq;	//kHz
	int volt;	//uV
	int div;
	int up_threshold;
};

struct gpu_reg_info {
	struct regmap *regmap;
	u32 args[2];
};

struct gpu_dfs_context {
	int gpu_clock_on;
	int gpu_power_on;
	int cur_load;
	int cur_voltage;

	struct device *dev;
	struct clk_bulk_data *clocks;
	struct clk *gpu_clock;
	int num_clocks;

	struct gpu_freq_info* freq_list;
	int freq_list_len;

	const struct gpu_freq_info* freq_cur;
	const struct gpu_freq_info* freq_next;

	const struct gpu_freq_info* freq_min;
	const struct gpu_freq_info* freq_max;
	const struct gpu_freq_info* freq_default;
	const struct gpu_freq_info* freq_9;
	const struct gpu_freq_info* freq_8;
	const struct gpu_freq_info* freq_7;
	const struct gpu_freq_info* freq_5;
	const struct gpu_freq_info* freq_range_max;
	const struct gpu_freq_info* freq_range_min;

	struct workqueue_struct *gpu_dfs_workqueue;
	struct semaphore* sem;

	struct gpu_reg_info top_reg;
};

#ifndef CONFIG_OF
static struct kbase_io_resources io_resources = {
	.job_irq_number = 68,
	.mmu_irq_number = 69,
	.gpu_irq_number = 70,
	.io_memory_region = {
	.start = 0x2F010000,
	.end = 0x2F010000 + (4096 * 4) - 1}
};
#endif

static DEFINE_SEMAPHORE(gpu_dfs_sem, 1);
static struct gpu_dfs_context gpu_dfs_ctx=
{
	.gpu_clock_on=0,
	.gpu_power_on=0,

	.sem=&gpu_dfs_sem,
};

static struct regulator *gpu_reg = NULL;

extern int gpu_boost_level;

extern int gpu_freq_cur;
extern int gpu_voltage_cur;
#ifdef CONFIG_MALI_MIDGARD_DVFS
extern int gpu_freq_min_limit;
extern int gpu_freq_max_limit;
#endif
extern char* gpu_freq_list;

static void gpu_freq_list_show(char *buf, size_t size)
{
	size_t len = 0;
	int i;

	for (i = 0; i < gpu_dfs_ctx.freq_list_len && len < size - 1; i++)
		len += scnprintf(buf + len, size - len, "%2d  %6d kHz %6d uV\n",
				 i, gpu_dfs_ctx.freq_list[i].freq,
				 gpu_dfs_ctx.freq_list[i].volt);
}

#define GPU_SUPPORT_QOS
#ifdef GPU_SUPPORT_QOS

#define ARQOS_THRESHOLD_MASK  0xFUL
#define AWQOS_THRESHOLD_MASK  0xFUL

static void __iomem *gpu_qos_reg;

static int   gpu_always_on = 0;
module_param_named(gpu_always_on, gpu_always_on, int, S_IRUGO | S_IWUSR);

static int   gpu_qos_threshold_aw = 6;
module_param_named(gpu_qos_threshold_aw, gpu_qos_threshold_aw, int, S_IRUGO | S_IWUSR);

static int   gpu_qos_threshold_ar = 6;
module_param_named(gpu_qos_threshold_ar, gpu_qos_threshold_ar, int, S_IRUGO | S_IWUSR);

static void config_gpu_qos(void)
{
	uint32_t reg_val = 0;
	reg_val = readl(gpu_qos_reg);
	reg_val &= ~(ARQOS_THRESHOLD_MASK << 8);
	reg_val |= (READ_ONCE(gpu_qos_threshold_ar) & ARQOS_THRESHOLD_MASK) << 8;

	reg_val &= ~(AWQOS_THRESHOLD_MASK << 12);
	reg_val |= (READ_ONCE(gpu_qos_threshold_aw) & AWQOS_THRESHOLD_MASK) << 12;
	writel(reg_val, gpu_qos_reg);

	KBASE_DEBUG_PRINT(4, "%s: gpu qos reg:0x%x \n", __func__,  readl(gpu_qos_reg));
}
#endif

static int mali_freq_init(struct device *dev)
{
	struct device_node *np = dev->of_node;
	static const struct {
		const char *property;
		const struct gpu_freq_info **target;
	} indexes[] = {
		{ "sprd,dfs-default", &gpu_dfs_ctx.freq_default },
		{ "sprd,dfs-scene-extreme", &gpu_dfs_ctx.freq_9 },
		{ "sprd,dfs-scene-high", &gpu_dfs_ctx.freq_8 },
		{ "sprd,dfs-scene-medium", &gpu_dfs_ctx.freq_7 },
		{ "sprd,dfs-scene-low", &gpu_dfs_ctx.freq_5 },
		{ "sprd,dfs-range-max", &gpu_dfs_ctx.freq_range_max },
		{ "sprd,dfs-range-min", &gpu_dfs_ctx.freq_range_min },
	};
	int count, i;

	if (of_property_match_string(np, "syscon-names", "top_force_shutdown") != 0)
		return dev_err_probe(dev, -EINVAL, "Invalid GPU power syscon binding\n");

	gpu_dfs_ctx.top_reg.regmap = syscon_regmap_lookup_by_phandle_args(np, "syscons",
									ARRAY_SIZE(gpu_dfs_ctx.top_reg.args),
									gpu_dfs_ctx.top_reg.args);
	if (IS_ERR(gpu_dfs_ctx.top_reg.regmap))
		return dev_err_probe(dev, PTR_ERR(gpu_dfs_ctx.top_reg.regmap),
				     "Failed to get GPU power syscon\n");
	if (!gpu_dfs_ctx.top_reg.args[1])
		return dev_err_probe(dev, -EINVAL, "Invalid GPU power shutdown mask\n");

	count = devm_clk_bulk_get_all(dev, &gpu_dfs_ctx.clocks);
	if (count < 0)
		return dev_err_probe(dev, count, "Failed to get GPU clocks\n");
	if (count <= DTS_CLK_OFFSET)
		return dev_err_probe(dev, -EINVAL, "Missing GPU clock sources\n");
	gpu_dfs_ctx.num_clocks = count;
	gpu_dfs_ctx.gpu_clock = gpu_dfs_ctx.clocks[1].clk;

	count = of_property_count_u32_elems(np, "sprd,dfs-lists");
	if (count <= 0 || count % 4)
		return dev_err_probe(dev, -EINVAL, "Invalid GPU DFS table size\n");

	gpu_dfs_ctx.freq_list_len = count / 4;
	gpu_dfs_ctx.freq_list = devm_kcalloc(dev, gpu_dfs_ctx.freq_list_len,
						    sizeof(*gpu_dfs_ctx.freq_list),
						    GFP_KERNEL);
	if (!gpu_dfs_ctx.freq_list)
		return -ENOMEM;

	for (i = 0; i < gpu_dfs_ctx.freq_list_len; i++) {
		struct gpu_freq_info *entry = &gpu_dfs_ctx.freq_list[i];
		u32 freq, volt, clock, div;

		if (of_property_read_u32_index(np, "sprd,dfs-lists", 4 * i, &freq) ||
		    of_property_read_u32_index(np, "sprd,dfs-lists", 4 * i + 1, &volt) ||
		    of_property_read_u32_index(np, "sprd,dfs-lists", 4 * i + 2, &clock) ||
		    of_property_read_u32_index(np, "sprd,dfs-lists", 4 * i + 3, &div) ||
		    !freq || freq > INT_MAX / 1000 || !volt || volt > INT_MAX ||
		    clock < DTS_CLK_OFFSET || clock >= gpu_dfs_ctx.num_clocks ||
		    !div || div > INT_MAX ||
		    (i && freq <= gpu_dfs_ctx.freq_list[i - 1].freq))
			return dev_err_probe(dev, -EINVAL, "Invalid GPU DFS entry %d\n", i);

		entry->clk_src = gpu_dfs_ctx.clocks[clock].clk;
		entry->freq = freq;
		entry->volt = volt;
		entry->div = div;
		entry->up_threshold = freq * UP_THRESHOLD;
	}

	for (i = 0; i < ARRAY_SIZE(indexes); i++) {
		u32 index;
		int err;

		err = of_property_read_u32(np, indexes[i].property, &index);
		if (err)
			return dev_err_probe(dev, err, "Missing %s\n", indexes[i].property);
		if (index >= gpu_dfs_ctx.freq_list_len)
			return dev_err_probe(dev, -EINVAL, "Invalid %s index\n", indexes[i].property);

		*indexes[i].target = &gpu_dfs_ctx.freq_list[index];
	}

	gpu_dfs_ctx.freq_max = gpu_dfs_ctx.freq_range_max;
	gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_range_min;
	if (gpu_dfs_ctx.freq_min > gpu_dfs_ctx.freq_max)
		return dev_err_probe(dev, -EINVAL, "Invalid GPU DFS range\n");

	gpu_dfs_ctx.freq_cur = gpu_dfs_ctx.freq_default;
	gpu_dfs_ctx.freq_next = gpu_dfs_ctx.freq_default;
	gpu_dfs_ctx.cur_voltage = gpu_dfs_ctx.freq_cur->volt;

#ifdef GPU_SUPPORT_QOS
	gpu_qos_reg = devm_ioremap(dev, 0x60100014, sizeof(u32));
	if (!gpu_qos_reg)
		return dev_err_probe(dev, -ENOMEM, "Failed to map GPU QoS register\n");
#endif
	return 0;
}

static int mali_power_on(void)
{
	struct gpu_reg_info *top = &gpu_dfs_ctx.top_reg;
	int err;

	err = regmap_update_bits(top->regmap, top->args[0], top->args[1], 0);
	if (err)
		return err;

	udelay(300);
	gpu_dfs_ctx.gpu_power_on = 1;
	return 0;
}

static int mali_power_off(void)
{
	struct gpu_reg_info *top = &gpu_dfs_ctx.top_reg;
	int err;

	err = regmap_update_bits(top->regmap, top->args[0], top->args[1], top->args[1]);
	if (!err)
		gpu_dfs_ctx.gpu_power_on = 0;

	return err;
}

static int mali_clock_on(void)
{
	struct clk_bulk_data *clocks = gpu_dfs_ctx.clocks;
	struct clk *clock = gpu_dfs_ctx.gpu_clock;
	const struct gpu_freq_info *freq = gpu_dfs_ctx.freq_cur;
	int i, err;

	for (i = DTS_CLK_OFFSET; i < gpu_dfs_ctx.num_clocks; i++) {
		err = clk_prepare_enable(clocks[i].clk);
		if (err)
			goto disable_sources;
	}

	err = clk_prepare_enable(clocks[0].clk);
	if (err)
		goto disable_sources;

	err = clk_prepare_enable(clock);
	if (err)
		goto disable_gate;

	udelay(300);

	err = clk_set_parent(clock, gpu_dfs_ctx.freq_default->clk_src);
	if (err)
		goto disable_core;

	err = clk_set_rate(freq->clk_src, freq->freq * 1000UL);
	if (err)
		goto disable_core;

	err = clk_set_parent(clock, freq->clk_src);
	if (err)
		goto disable_core;

#ifdef GPU_SUPPORT_QOS
	config_gpu_qos();
#endif
	gpu_dfs_ctx.gpu_clock_on = 1;
	gpu_freq_cur = freq->freq;
	return 0;

disable_core:
	clk_disable_unprepare(clock);
disable_gate:
	clk_disable_unprepare(clocks[0].clk);
disable_sources:
	while (i > DTS_CLK_OFFSET)
		clk_disable_unprepare(clocks[--i].clk);

	return err;
}

static void mali_clock_off(void)
{
	int i;

	gpu_freq_cur = 0;
	gpu_dfs_ctx.gpu_clock_on = 0;
	clk_disable_unprepare(gpu_dfs_ctx.gpu_clock);
	clk_disable_unprepare(gpu_dfs_ctx.clocks[0].clk);
	for (i = DTS_CLK_OFFSET; i < gpu_dfs_ctx.num_clocks; i++)
		clk_disable_unprepare(gpu_dfs_ctx.clocks[i].clk);
}

static int mali_platform_init(struct kbase_device *kbdev)
{
	struct device *dev = kbdev->dev;
	char *freq_list;
	int err;

	down(gpu_dfs_ctx.sem);
	if (gpu_dfs_ctx.dev) {
		err = -EBUSY;
		goto unlock;
	}

	err = mali_freq_init(dev);
	if (err)
		goto unlock;

	gpu_reg = devm_regulator_get(dev, "gpu");
	if (IS_ERR(gpu_reg)) {
		err = dev_err_probe(dev, PTR_ERR(gpu_reg), "Failed to get GPU regulator\n");
		goto unlock;
	}

	freq_list = devm_kzalloc(dev, PAGE_SIZE, GFP_KERNEL);
	if (!freq_list) {
		err = -ENOMEM;
		goto unlock;
	}
	gpu_freq_list_show(freq_list, PAGE_SIZE);

#ifdef CONFIG_MALI_MIDGARD_DVFS
	gpu_dfs_ctx.gpu_dfs_workqueue = alloc_ordered_workqueue("gpu_dfs", WQ_MEM_RECLAIM);
	if (!gpu_dfs_ctx.gpu_dfs_workqueue) {
		err = -ENOMEM;
		goto unlock;
	}
#endif

	err = mali_power_on();
	if (err) {
		dev_err(dev, "Failed to power on GPU: %d\n", err);
		goto destroy_workqueue;
	}

	err = mali_clock_on();
	if (err) {
		dev_err(dev, "Failed to start GPU clocks: %d\n", err);
		goto power_off;
	}

#ifdef CONFIG_INIT_VOLTAGE
	err = regulator_set_voltage(gpu_reg, gpu_dfs_ctx.cur_voltage, gpu_dfs_ctx.cur_voltage);
	if (err) {
		dev_err(dev, "Failed to set initial GPU voltage: %d\n", err);
		mali_clock_off();
		goto power_off;
	}
#endif

	gpu_dfs_ctx.dev = dev;
	gpu_voltage_cur = gpu_dfs_ctx.cur_voltage;
	kernel_param_lock(THIS_MODULE);
	gpu_freq_list = freq_list;
	kernel_param_unlock(THIS_MODULE);
#ifdef CONFIG_MALI_HOTPLUG
	kbase_hotplug_wake_up();
#endif
	up(gpu_dfs_ctx.sem);
	return 0;

power_off:
	if (mali_power_off())
		dev_warn(dev, "Failed to power off GPU after failed initialization\n");
destroy_workqueue:
#ifdef CONFIG_MALI_MIDGARD_DVFS
	destroy_workqueue(gpu_dfs_ctx.gpu_dfs_workqueue);
#endif
unlock:
	up(gpu_dfs_ctx.sem);
	return err;
}

static void mali_platform_term(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_MIDGARD_DVFS
	destroy_workqueue(gpu_dfs_ctx.gpu_dfs_workqueue);
#endif
	down(gpu_dfs_ctx.sem);
	if (gpu_dfs_ctx.gpu_clock_on)
		mali_clock_off();
	if (gpu_dfs_ctx.gpu_power_on && mali_power_off())
		dev_warn(kbdev->dev, "Failed to power off GPU\n");

	kernel_param_lock(THIS_MODULE);
	gpu_freq_list = NULL;
	kernel_param_unlock(THIS_MODULE);
	gpu_dfs_ctx.dev = NULL;
	up(gpu_dfs_ctx.sem);
}

struct kbase_platform_funcs_conf platform_sharkle_funcs = {
	.platform_init_func = mali_platform_init,
	.platform_term_func = mali_platform_term
};

static int mali_power_mode_change(struct kbase_device *kbdev, bool on)
{
	bool powered_here = false;
	int err = 0;

	down(gpu_dfs_ctx.sem);
	if (on) {
		if (!gpu_dfs_ctx.gpu_power_on) {
			err = mali_power_on();
			if (err)
				goto unlock;
			powered_here = true;
		}

		if (!gpu_dfs_ctx.gpu_clock_on) {
			err = mali_clock_on();
			if (err) {
				if (powered_here && mali_power_off())
					dev_warn(kbdev->dev, "Failed to power off GPU after clock failure\n");
				goto unlock;
			}
		}
#ifdef CONFIG_MALI_HOTPLUG
		kbase_hotplug_wake_up();
#endif
	} else if (!READ_ONCE(gpu_always_on)) {
		if (gpu_dfs_ctx.gpu_clock_on)
			mali_clock_off();
		if (gpu_dfs_ctx.gpu_power_on)
			err = mali_power_off();
	}

unlock:
	up(gpu_dfs_ctx.sem);
	if (err)
		dev_err(kbdev->dev, "Failed to switch GPU %s: %d\n", on ? "on" : "off", err);
	return err;
}

#ifdef KBASE_PM_RUNTIME
/* The core serializes power transitions and runs early probe callbacks serially. */
static bool gpu_runtime_ref;

static void mali_runtime_put(struct device *dev, bool system)
{
	int err;

	if (!gpu_runtime_ref)
		return;

	gpu_runtime_ref = false;
	if (system) {
		pm_runtime_put_noidle(dev);
		return;
	}

	err = pm_runtime_put_sync(dev);
	if (err < 0)
		dev_warn(dev, "Failed to release runtime PM reference: %d\n", err);
}
#endif

static void pm_callback_power_off(struct kbase_device *kbdev)
{
#ifdef KBASE_PM_RUNTIME
	mali_runtime_put(kbdev->dev, false);
#endif
	mali_power_mode_change(kbdev, false);
}

static int pm_callback_power_on(struct kbase_device *kbdev)
{
	int err;

	err = mali_power_mode_change(kbdev, true);
	if (err)
		return err;

#ifdef KBASE_PM_RUNTIME
	if (!gpu_runtime_ref) {
		err = pm_runtime_resume_and_get(kbdev->dev);
		if (err < 0) {
			dev_err(kbdev->dev, "Failed to resume GPU runtime PM: %d\n", err);
			mali_power_mode_change(kbdev, false);
			return err;
		}
		gpu_runtime_ref = true;
	}
#endif
	return 1;
}

static void pm_callback_power_suspend(struct kbase_device *kbdev)
{
#ifdef KBASE_PM_RUNTIME
	mali_runtime_put(kbdev->dev, true);
#endif
	mali_power_mode_change(kbdev, false);
}

static int pm_callback_power_resume(struct kbase_device *kbdev)
{
	int err = mali_power_mode_change(kbdev, true);

#ifdef KBASE_PM_RUNTIME
	if (!err && !gpu_runtime_ref) {
		pm_runtime_get_noresume(kbdev->dev);
		gpu_runtime_ref = true;
	}
#endif
	return err;
}

#ifdef KBASE_PM_RUNTIME
static int pm_callback_power_runtime_init(struct kbase_device *kbdev)
{
	int err;

	err = pm_runtime_set_active(kbdev->dev);
	if (err)
		return err;
	pm_suspend_ignore_children(kbdev->dev, true);
	pm_runtime_set_autosuspend_delay(kbdev->dev, PM_RUNTIME_DELAY_MS);
	pm_runtime_use_autosuspend(kbdev->dev);
	pm_runtime_enable(kbdev->dev);

	return 0;
}

static void pm_callback_power_runtime_term(struct kbase_device *kbdev)
{
	KBASE_DEBUG_PRINT(4, "mali------------------- pm_callback_power_runtime_term\n");
	mali_runtime_put(kbdev->dev, true);
	pm_runtime_disable(kbdev->dev);
}
#endif/*CONFIG_PM_RUNTIME*/

struct kbase_pm_callback_conf pm_sharkle_callbacks = {
	.power_off_callback = pm_callback_power_off,
	.power_on_callback = pm_callback_power_on,
	.power_suspend_callback = pm_callback_power_suspend,
	.power_resume_callback = pm_callback_power_resume,
#ifdef KBASE_PM_RUNTIME
	.power_runtime_init_callback = pm_callback_power_runtime_init,
	.power_runtime_term_callback = pm_callback_power_runtime_term,
	.power_runtime_off_callback = NULL,
	.power_runtime_on_callback = NULL
#endif
};


static struct kbase_platform_config versatile_platform_config = {
#ifndef CONFIG_OF
	.io_resources = &io_resources
#endif
};

struct kbase_platform_config *kbase_get_platform_config(void)
{
	return &versatile_platform_config;
}

#if defined(CONFIG_MALI_MIDGARD_DVFS) || defined(CONFIG_MALI_DEVFREQ)
#ifdef CONFIG_ADJUST_VOLTAGE
static int mali_set_voltage(int target)
{
	int err;

	if (target == gpu_dfs_ctx.cur_voltage)
		return 0;

	err = regulator_set_voltage(gpu_reg, target, target);
	if (err)
		return err;

	gpu_dfs_ctx.cur_voltage = target;
	gpu_voltage_cur = target;
	return 0;
}
#endif

static int freq_search(struct gpu_freq_info freq_list[], int len, int key)
{
	int low=0, high=len-1, mid;

	if (0 > key)
	{
		return -1;
	}

	while(low <= high)
	{
		mid = (low+high)/2;
		if(key == freq_list[mid].freq)
		{
			return mid;
		}

		if(key < freq_list[mid].freq)
		{
			high = mid-1;
		}
		else
		{
			low = mid+1;
		}
	}
	return -1;
}

static int gpu_change_freq(void)
{
	const struct gpu_freq_info *old = gpu_dfs_ctx.freq_cur;
	const struct gpu_freq_info *next = gpu_dfs_ctx.freq_next;
	struct clk *clock = gpu_dfs_ctx.gpu_clock;
	int err;

	if (old == next)
		return 0;

	if (old->clk_src == next->clk_src) {
		err = clk_set_parent(clock, gpu_dfs_ctx.freq_default->clk_src);
		if (err)
			return err;
	}

	err = clk_set_rate(next->clk_src, next->freq * 1000UL);
	if (err)
		goto restore_parent;

	err = clk_set_parent(clock, next->clk_src);
	if (err) {
		if (old->clk_src == next->clk_src &&
		    clk_set_rate(old->clk_src, old->freq * 1000UL))
			dev_warn(gpu_dfs_ctx.dev, "Failed to restore GPU clock rate\n");
		goto restore_parent;
	}

	gpu_dfs_ctx.freq_cur = next;
	gpu_freq_cur = next->freq;
#ifdef CONFIG_MALI_SPRD_CLOCK_TRACE
	trace_clock_set_rate(VENDOR_FTRACE_MODULE_NAME, next->freq, raw_smp_processor_id());
#endif
	return 0;

restore_parent:
	if (clk_set_parent(clock, old->clk_src))
		dev_warn(gpu_dfs_ctx.dev, "Failed to restore GPU clock parent\n");
	return err;
}

static int gpu_change_volt_freq(void)
{
	int err = 0;
#ifdef CONFIG_ADJUST_VOLTAGE
	int target_voltage;
#endif

	down(gpu_dfs_ctx.sem);

#ifdef CONFIG_ADJUST_VOLTAGE
	target_voltage = gpu_dfs_ctx.freq_next->volt;
	if (target_voltage > gpu_dfs_ctx.cur_voltage) {
		err = mali_set_voltage(target_voltage);
		if (err)
			goto unlock;
	}
#endif

	if (gpu_dfs_ctx.gpu_power_on && gpu_dfs_ctx.gpu_clock_on)
		err = gpu_change_freq();
	else
		gpu_dfs_ctx.freq_cur = gpu_dfs_ctx.freq_next;

#ifdef CONFIG_ADJUST_VOLTAGE
	if (!err && target_voltage < gpu_dfs_ctx.cur_voltage)
		err = mali_set_voltage(target_voltage);
unlock:
#endif
	up(gpu_dfs_ctx.sem);
	return err;
}
#endif

#ifdef CONFIG_MALI_MIDGARD_DVFS
static const struct gpu_freq_info* get_next_freq(const struct gpu_freq_info* min_freq, const struct gpu_freq_info* max_freq, int target)
{
	const struct gpu_freq_info* freq;

	for (freq = min_freq; freq <= max_freq; freq++)
	{
		if (freq->up_threshold > target)
		{
			return freq;
		}
	}
	return max_freq;
}

static void gpu_dfs_func(struct work_struct *work)
{
	gpu_change_volt_freq();
}

static DECLARE_WORK(gpu_dfs_work, &gpu_dfs_func);

int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation,
	u32 util_gl_share, u32 util_cl_share[2])
{
	int result = 0, min_index = -1, max_index = -1;

	gpu_dfs_ctx.cur_load = utilisation;

	KBASE_DEBUG_PRINT(3, "MALI_DVFS utilisation=%d util_gl_share=%d, util_cl_share[0]=%d, util_cl_share[1]=%d \n",
		utilisation, util_gl_share, util_cl_share[0], util_cl_share[1]);
	KBASE_DEBUG_PRINT(3, "MALI_DVFS gpu_boost_level:%d \n", gpu_boost_level);

	switch(gpu_boost_level)
	{
	case 10:
		gpu_dfs_ctx.freq_max =
		gpu_dfs_ctx.freq_min = &gpu_dfs_ctx.freq_list[gpu_dfs_ctx.freq_list_len-1];
		break;

	case 9:
		gpu_dfs_ctx.freq_max =
		gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_9;
		break;

	case 7:
		gpu_dfs_ctx.freq_max =
		gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_7;
		break;

	case 5:
		gpu_dfs_ctx.freq_max =
		gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_5;
		break;

	case 0:
	default:
		gpu_dfs_ctx.freq_max = gpu_dfs_ctx.freq_range_max;
		gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_range_min;
		break;
	}

	gpu_boost_level = 0;

	//limit min freq
	min_index = freq_search(gpu_dfs_ctx.freq_list, gpu_dfs_ctx.freq_list_len, gpu_freq_min_limit);
	if ((0 <= min_index) &&
		(gpu_dfs_ctx.freq_min->freq < gpu_dfs_ctx.freq_list[min_index].freq))
	{
		gpu_dfs_ctx.freq_min = &gpu_dfs_ctx.freq_list[min_index];
		if (gpu_dfs_ctx.freq_min->freq > gpu_dfs_ctx.freq_max->freq)
		{
			gpu_dfs_ctx.freq_max = gpu_dfs_ctx.freq_min;
		}
	}

	//limit max freq
	max_index = freq_search(gpu_dfs_ctx.freq_list, gpu_dfs_ctx.freq_list_len, gpu_freq_max_limit);
	if ((0 <= max_index) &&
		(gpu_dfs_ctx.freq_max->freq > gpu_dfs_ctx.freq_list[max_index].freq))
	{
		gpu_dfs_ctx.freq_max = &gpu_dfs_ctx.freq_list[max_index];
		if (gpu_dfs_ctx.freq_max->freq < gpu_dfs_ctx.freq_min->freq)
		{
			gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_max;
		}
	}

	// if the loading ratio is greater then 90%, switch the clock to the maximum
	if(gpu_dfs_ctx.cur_load >= (100*UP_THRESHOLD))
	{
		gpu_dfs_ctx.freq_next = gpu_dfs_ctx.freq_max;
	}
	else
	{
		int target_freq = gpu_dfs_ctx.freq_cur->freq * gpu_dfs_ctx.cur_load / 100;
		gpu_dfs_ctx.freq_next = get_next_freq(gpu_dfs_ctx.freq_min, gpu_dfs_ctx.freq_max, target_freq);
	}

	KBASE_DEBUG_PRINT(3, "MALI_DVFS util %3d; cur_freq %6d -> next_freq %6d\n",
		gpu_dfs_ctx.cur_load, gpu_dfs_ctx.freq_cur->freq, gpu_dfs_ctx.freq_next->freq);

	if(gpu_dfs_ctx.freq_next->freq != gpu_dfs_ctx.freq_cur->freq)
	{
		queue_work(gpu_dfs_ctx.gpu_dfs_workqueue, &gpu_dfs_work);
	}

	return (result);
}
#endif/*CONFIG_MALI_MIDGARD_DVFS*/

#ifdef CONFIG_MALI_DEVFREQ
#define FREQ_KHZ    1000

int kbase_platform_get_init_freq(void)
{
	return (gpu_dfs_ctx.freq_default->freq * FREQ_KHZ);
}

int kbase_platform_set_freq(int freq)
{
	int result = -1, index = -1;

	freq = freq/FREQ_KHZ;
	index = freq_search(gpu_dfs_ctx.freq_list, gpu_dfs_ctx.freq_list_len, freq);
	KBASE_DEBUG_PRINT(2, "GPU_DVFS  kbase_platform_set_freq index=%d! freq=%d \n", index, freq);
	if (0 <= index)
	{
		gpu_dfs_ctx.freq_next = &gpu_dfs_ctx.freq_list[index];
		result = gpu_change_volt_freq();
	}

	return (result);
}

#ifdef CONFIG_MALI_BOOST
void kbase_platform_modify_target_freq(struct device *dev, unsigned long *target_freq)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	unsigned long min_freq, max_freq;
	int min_index, max_index;

	if (!kbdev->devfreq)
		return;

	devfreq_get_freq_range(kbdev->devfreq, &min_freq, &max_freq);

	switch(gpu_boost_level)
	{
	case 10:
		gpu_dfs_ctx.freq_max =
		gpu_dfs_ctx.freq_min = &gpu_dfs_ctx.freq_list[gpu_dfs_ctx.freq_list_len-1];
		break;

	case 9:
		gpu_dfs_ctx.freq_max =
		gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_9;
		break;

	case 7:
		gpu_dfs_ctx.freq_max =
		gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_7;
		break;

	case 5:
		gpu_dfs_ctx.freq_max =
		gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_5;
		break;

	case 0:
	default:
		gpu_dfs_ctx.freq_max = gpu_dfs_ctx.freq_range_max;
		gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_range_min;
		break;
	}

	//limit min freq
	min_index = freq_search(gpu_dfs_ctx.freq_list, gpu_dfs_ctx.freq_list_len, min_freq / FREQ_KHZ);
	if ((0 <= min_index) &&
		(gpu_dfs_ctx.freq_min->freq < gpu_dfs_ctx.freq_list[min_index].freq))
	{
		gpu_dfs_ctx.freq_min = &gpu_dfs_ctx.freq_list[min_index];
		if (gpu_dfs_ctx.freq_min->freq > gpu_dfs_ctx.freq_max->freq)
		{
			gpu_dfs_ctx.freq_max = gpu_dfs_ctx.freq_min;
		}
	}

	//limit max freq
	max_index = freq_search(gpu_dfs_ctx.freq_list, gpu_dfs_ctx.freq_list_len, max_freq / FREQ_KHZ);
	if ((0 <= max_index) &&
		(gpu_dfs_ctx.freq_max->freq > gpu_dfs_ctx.freq_list[max_index].freq))
	{
		gpu_dfs_ctx.freq_max = &gpu_dfs_ctx.freq_list[max_index];
		if (gpu_dfs_ctx.freq_max->freq < gpu_dfs_ctx.freq_min->freq)
		{
			gpu_dfs_ctx.freq_min = gpu_dfs_ctx.freq_max;
		}
	}

	KBASE_DEBUG_PRINT(3, "GPU_DVFS kbase_platform_modify_target_freq gpu_boost_level:%d min_freq=%d max_freq=%d target_freq=%lu\n",
		gpu_boost_level, gpu_dfs_ctx.freq_min->freq, gpu_dfs_ctx.freq_max->freq, *target_freq);

	gpu_boost_level = 0;

	//set target frequency
	if (*target_freq < gpu_dfs_ctx.freq_min->freq*FREQ_KHZ)
	{
		*target_freq = gpu_dfs_ctx.freq_min->freq*FREQ_KHZ;
	}
	if (*target_freq > gpu_dfs_ctx.freq_max->freq*FREQ_KHZ)
	{
		*target_freq = gpu_dfs_ctx.freq_max->freq*FREQ_KHZ;
	}
	*target_freq = clamp(*target_freq, min_freq, max_freq);
}
#endif
#endif

bool kbase_mali_is_powered(void)
{
	bool result = false;

	down(gpu_dfs_ctx.sem);
	if (gpu_dfs_ctx.gpu_power_on && gpu_dfs_ctx.gpu_clock_on)
	{
		result = true;
	}
	up(gpu_dfs_ctx.sem);

	return (result);
}

#ifdef CONFIG_MALI_BOOST
void kbase_platform_set_boost(struct kbase_device *kbdev, int boost_level)
{
	if (gpu_boost_level < boost_level)
	{
		gpu_boost_level = boost_level;
		KBASE_DEBUG_PRINT(3, "kbase_platform_set_boost gpu_boost_level =%d\n", gpu_boost_level);
	}
}
#endif
