#ifndef _PWM_SPRD_
#define _PWM_SPRD_
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/sprd-glb.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define PWM_MOD_MAX	0xff
#define PWM_REG_MSK	0xffff

/**
 * struct sprd_pwm_chip - struct representing pwm chip
 *
 * @mmio_base: base address of pwm chip
 * @clk: pointer to clk structure of pwm chip
 * @syscon: syscon of pwm chip
 * @core_ops: register operations for this hardware version
 */
struct sprd_pwm_chip {
	void __iomem *mmio_base;
	struct clk *clk;
	struct regmap *syscon;
	const struct pwm_core_ops *core_ops;
};

struct pwm_core_ops {
	void (*pwm_early_config)(struct pwm_chip *chip,
		struct pwm_device *pwm, bool enable);
	void (*pwm_config)(struct pwm_chip *chip,
		struct pwm_device *pwm, int level, int precale);
};

extern const struct pwm_core_ops pwm_r2p0_ops;
extern const struct pwm_core_ops pwm_r3p0_ops;

static inline struct sprd_pwm_chip *to_sprd_pwm_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static inline uint32_t sprd_pwm_readl(struct sprd_pwm_chip *chip, uint32_t num,
				  uint32_t offset)
{
	return readl_relaxed((void __iomem *)(chip->mmio_base +
						(num << 5) + offset));
}

static inline void sprd_pwm_writel(struct sprd_pwm_chip *chip,
					uint32_t num, uint32_t offset,
					uint32_t val)
{
	writel_relaxed(val, (void __iomem *)(chip->mmio_base +
						(num << 5) + offset));
}
#endif
