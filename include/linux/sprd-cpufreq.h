#ifndef __SPRD_CPUFREQ_H__
#define __SPRD_CPUFREQ_H__

#ifdef CONFIG_ARM_SPRD_CPUFREQ
unsigned int sprd_cpufreq_update_opp(int cpu, int temp_now);
#else
static unsigned int sprd_cpufreq_update_opp(int cpu, int temp_now)
{
	return 0;
}
#endif

struct regulator;

void down_voltage_sem(void);
void up_voltage_sem(void);
struct regulator *get_cpu_gpu_regulator(void);
#endif

