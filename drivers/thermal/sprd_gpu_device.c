/*
 * Copyright (C) 2012-2015 Spreadtrum Communications Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/devfreq_cooling.h>
#include <linux/energy_model.h>
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/thermal.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/sprd_otp.h>
#include <linux/debugfs.h>
#include <linux/printk.h>
#include <linux/sprd_gpu_device.h>

/* The DW99 GPU thermal zone has a 110 C critical trip. */
#define MAX_STATIC_TEMPERATURE_C 110
#define GPU_CLUSTER 0

/*
 * Tscale = aT^3 + bT^2 + cT + d
 * Vscale = aV^3 + bV^2 + cV + d
 */
struct scale_coeff {
	int scale_a;
	int scale_b;
	int scale_c;
	int scale_d;
};

/* Pdyn = dynperghz * freq * (V/Vbase)^2 */
struct dyn_power_coeff {
	int dynperghz;
	int freq;
	int voltage_base;
};

struct cluster_power_coefficients {
	u32 hotplug_period;
	int leak_core_base;
	int leak_cluster_base;
	struct scale_coeff temp_scale;
	struct scale_coeff voltage_scale;
	struct dyn_power_coeff core_coeff;
	struct dyn_power_coeff cluster_coeff;
};

static struct cluster_power_coefficients gpu_coefficients;
static struct thermal_cooling_device *gpu_cdev;
static struct device *gpu_em_device;
static unsigned int gpu_cores;
static DEFINE_MUTEX(gpu_cooling_lock);

/* return (leak * 10)  */
static int get_gpu_static_power_coeff(void)
{
	return gpu_coefficients.leak_core_base;
}

/* return (leak * 10)  */
static int get_cache_static_power_coeff(void)
{
	return gpu_coefficients.leak_cluster_base;
}

static u64 get_core_dyn_power(int cluster_id,
	unsigned int freq_mhz, unsigned int voltage_mv)
{
	u64 power;
	u32 voltage_base = gpu_coefficients.core_coeff.voltage_base;
	u32 dyn_base = gpu_coefficients.core_coeff.dynperghz;

	if (!dyn_base)
		return 0;

	power = (u64)dyn_base * freq_mhz * voltage_mv * voltage_mv;
	power = div64_u64(power, (u64)voltage_base * voltage_base);
	do_div(power, 10000);

	return power;
}

static u64 get_cluster_dyn_power(int cluster_id,
	unsigned int freq_mhz, unsigned int voltage_mv)
{
	u64 power;
	u32 voltage_base = gpu_coefficients.cluster_coeff.voltage_base;
	u32 dyn_base = gpu_coefficients.cluster_coeff.dynperghz;

	if (!dyn_base)
		return 0;

	power = (u64)dyn_base * freq_mhz * voltage_mv * voltage_mv;
	power = div64_u64(power, (u64)voltage_base * voltage_base);
	do_div(power, 10000);

	pr_debug("cluster:%d cluster_dyn_power:%u freq:%u voltage:%u voltage_base:%d\n",
		cluster_id, (u32)power, freq_mhz, voltage_mv, voltage_base);

	return power;
}

/*
 *Tscale = 0.0000825T^3 - 0.0117T^2 + 0.608T - 8.185
 * return Tscale * 1000
 */
static u64 get_temperature_scale(s64 temp)
{
	s64 t_scale;
	struct scale_coeff *coeff = &gpu_coefficients.temp_scale;

	t_scale = coeff->scale_a * temp * temp * temp
		+ coeff->scale_b * temp * temp
		+ coeff->scale_c * temp
		+ coeff->scale_d;

	return t_scale > 0 ? div_s64(t_scale, 10000) : 0;
}

/*
 * Vscale = 33.31V^3 - 73.25V^2 + 54.44V - 12.81
 * return Vscale * 1000
 */
static u64 get_voltage_scale(unsigned long u_volt)
{
	s64 m_volt = u_volt / 1000;
	s64 v_scale;
	struct scale_coeff *coeff = &gpu_coefficients.voltage_scale;

	v_scale = coeff->scale_a * m_volt * m_volt * m_volt / 1000
		+ coeff->scale_b * m_volt * m_volt
		+ coeff->scale_c * m_volt * 1000
		+ coeff->scale_d * 1000 * 1000;

	return v_scale > 0 ? div_s64(v_scale, 100000) : 0;
}

/*
 * Use the critical-trip temperature for the leakage estimate. The thermal
 * governor still reads the live sensor, and the power callback can run while
 * that thermal zone is locked.
 */
static u64 get_static_power(unsigned long u_volt)
{
	u64 t_scale = get_temperature_scale(MAX_STATIC_TEMPERATURE_C);
	u64 v_scale = get_voltage_scale(u_volt);
	u64 coeff = (u64)gpu_cores * get_gpu_static_power_coeff() +
		    get_cache_static_power_coeff();

	return div64_u64(coeff * t_scale * v_scale, 10000000);
}

#if defined(CONFIG_OTP_SPRD_AP_EFUSE_I)
/* return (leakage * 10) */
static u64 get_leak_base(int cluster_id, int val, int *coeff)
{
	int i;
	u64 leak_base;

	if (cluster_id)
		leak_base = ((val>>16) & 0x1F) + 1;
	else
		leak_base = ((val>>11) & 0x1F) + 1;

	/* (LIT_LEAK[4:0]+1) x 2mA x 0.85V x 18.69% */
	for (i = 0; i < 3; i++)
		leak_base = leak_base * coeff[i];
	do_div(leak_base, 100000);

	return leak_base;
}
#endif

static u64 get_dynamic_power(unsigned long freq, unsigned long voltage)
{
	unsigned int freq_mhz = freq / 1000000;

	return get_core_dyn_power(GPU_CLUSTER, freq_mhz, voltage) +
	       get_cluster_dyn_power(GPU_CLUSTER, freq_mhz, voltage);
}

static unsigned long gpu_power_mw(unsigned long freq, unsigned long voltage,
				  unsigned long busy, unsigned long total)
{
	u64 dynamic = get_dynamic_power(freq, voltage);

	if (total)
		dynamic = mul_u64_u64_div_u64(dynamic, min(busy, total), total);

	return clamp_t(u64, get_static_power(voltage * 1000) + dynamic, 1,
		       EM_MAX_POWER / 1000);
}

static int gpu_opp_power(struct device *dev, unsigned long *power,
			 unsigned long *freq)
{
	unsigned long hz = *freq * 1000;
	unsigned long voltage;
	struct dev_pm_opp *opp;

	opp = dev_pm_opp_find_freq_ceil(dev, &hz);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	voltage = dev_pm_opp_get_voltage(opp) / 1000;
	dev_pm_opp_put(opp);
	if (!voltage)
		return -EINVAL;

	*freq = hz / 1000;
	*power = gpu_power_mw(hz, voltage, 0, 0) * 1000;
	return 0;
}

static int gpu_real_power(struct devfreq *df, u32 *power,
			  unsigned long freq, unsigned long voltage)
{
	struct devfreq_dev_status status;

	mutex_lock(&df->lock);
	status = df->last_status;
	mutex_unlock(&df->lock);
	*power = gpu_power_mw(freq, voltage, status.busy_time, status.total_time);
	return 0;
}

static struct devfreq_cooling_power power_model_ops = {
	.get_real_power = gpu_real_power,
};

static int sprd_get_power_model_coeff(struct device_node *np,
		struct cluster_power_coefficients *power_coeff, int cluster_id)
{
	int ret;
	int val = 0;
#if defined(CONFIG_OTP_SPRD_AP_EFUSE_I)
	int efuse_block = -1;
	int coeff[3];
#endif
	if (!np) {
		pr_err("device node not found\n");
		return -EINVAL;
	}

#if defined(CONFIG_OTP_SPRD_AP_EFUSE_I)
	ret = of_property_read_u32(np, "sprd,efuse-block15", &efuse_block);
	if (ret) {
		pr_err("fail to get cooling devices efuse_block\n");
		efuse_block = -1;
	}

	if (efuse_block >= 0)
		val = sprd_ap_efuse_read(efuse_block);

	pr_debug("sci_efuse_leak --val : %x\n", val);
	if (val) {
		ret = of_property_read_u32_array(np,
				"sprd,leak-core", coeff, 3);
		if (ret) {
			pr_err("fail to get cooling devices leak-core-coeff\n");
			return -EINVAL;
		}

		power_coeff->leak_core_base =
			get_leak_base(cluster_id, val, coeff);

		ret = of_property_read_u32_array(np,
				"sprd,leak-cluster", coeff, 3);
		if (ret) {
			pr_err("fail to get cooling devices leak-cluster-coeff\n");
			return -EINVAL;
		}

		power_coeff->leak_cluster_base =
			get_leak_base(cluster_id, val, coeff);
	}
#endif

	if (!val) {
		ret = of_property_read_u32(np, "sprd,core-base",
				&power_coeff->leak_core_base);
		if (ret) {
			pr_err("fail to get def cool-dev leak-core-base\n");
			return -EINVAL;
		}

		ret = of_property_read_u32(np, "sprd,cluster-base",
				&power_coeff->leak_cluster_base);
		if (ret) {
			pr_err("fail to get def cool-dev leak-cluster-base\n");
			return -EINVAL;
		}
	}

	ret = of_property_read_u32_array(np, "sprd,temp-scale",
			(int *)&power_coeff->temp_scale,
			sizeof(struct scale_coeff) / sizeof(int));
	if (ret) {
		pr_err("fail to get cooling devices temp-scale\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(np, "sprd,voltage-scale",
			(int *)&power_coeff->voltage_scale,
			sizeof(struct scale_coeff) / sizeof(int));
	if (ret) {
		pr_err("fail to get cooling devices voltage-scale\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(np, "sprd,dynamic-core",
			(int *)&power_coeff->core_coeff,
			sizeof(struct dyn_power_coeff) / sizeof(int));
	if (ret) {
		pr_err("fail to get cooling devices dynamic-core-coeff\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(np, "sprd,dynamic-cluster",
			(int *)&power_coeff->cluster_coeff,
			sizeof(struct dyn_power_coeff) / sizeof(int));
	if (ret) {
		pr_err("fail to get cooling devices dynamic-cluster-coeff\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np,
		"sprd,hotplug-period", &power_coeff->hotplug_period);
	if (ret)
		pr_err("fail to get cooling devices efuse_block\n");

	return 0;
}

int create_gpu_cooling_device(struct devfreq *gpudev, u64 *mask)
{
	static const struct em_data_callback em_cb = EM_DATA_CB(gpu_opp_power);
	struct device_node *np, *child;
	struct device *dev;
	int nr_opps, ret;

	if (!gpudev || !mask)
		return -EINVAL;

	np = of_find_node_by_name(NULL, "gpu-cooling-devices");
	if (!np)
		return 0;

	child = of_get_compatible_child(np, "sprd,mali-power-model");
	of_node_put(np);
	if (!child)
		return -ENODEV;
	if (!of_device_is_available(child)) {
		ret = 0;
		goto put_child;
	}

	mutex_lock(&gpu_cooling_lock);
	if (gpu_cdev) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = sprd_get_power_model_coeff(child, &gpu_coefficients, GPU_CLUSTER);
	if (ret)
		goto unlock;
	if (gpu_coefficients.leak_core_base < 0 ||
	    gpu_coefficients.leak_cluster_base < 0 ||
	    gpu_coefficients.core_coeff.dynperghz < 0 ||
	    gpu_coefficients.cluster_coeff.dynperghz < 0 ||
	    (gpu_coefficients.core_coeff.dynperghz &&
	     gpu_coefficients.core_coeff.voltage_base <= 0) ||
	    (gpu_coefficients.cluster_coeff.dynperghz &&
	     gpu_coefficients.cluster_coeff.voltage_base <= 0)) {
		ret = -EINVAL;
		goto unlock;
	}

	gpu_cores = max_t(unsigned int, hweight64(READ_ONCE(*mask)), 1);
	dev = gpudev->dev.parent;

	nr_opps = dev_pm_opp_get_opp_count(dev);
	if (nr_opps <= 0) {
		ret = nr_opps ?: -ENODEV;
		goto unlock;
	}

	if (!em_pd_get(dev)) {
		ret = em_dev_register_perf_domain(dev, nr_opps, &em_cb, NULL, true);
		if (ret)
			goto unlock;
		gpu_em_device = dev;
	}

	gpu_cdev = of_devfreq_cooling_register_power(child, gpudev, &power_model_ops);
	if (IS_ERR(gpu_cdev)) {
		ret = PTR_ERR(gpu_cdev);
		gpu_cdev = NULL;
		if (gpu_em_device) {
			em_dev_unregister_perf_domain(gpu_em_device);
			gpu_em_device = NULL;
		}
	}

unlock:
	mutex_unlock(&gpu_cooling_lock);
put_child:
	of_node_put(child);
	return ret;
}
EXPORT_SYMBOL_GPL(create_gpu_cooling_device);

int destroy_gpu_cooling_device(void)
{
	mutex_lock(&gpu_cooling_lock);
	if (gpu_cdev) {
		devfreq_cooling_unregister(gpu_cdev);
		gpu_cdev = NULL;
	}
	if (gpu_em_device) {
		em_dev_unregister_perf_domain(gpu_em_device);
		gpu_em_device = NULL;
	}
	mutex_unlock(&gpu_cooling_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(destroy_gpu_cooling_device);
